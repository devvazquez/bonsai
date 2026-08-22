#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>

#define WIFI_MAX_NETWORKS 5

enum class WiFiStatus {
    Connected,
    Disconnected,
    Reconnecting,
};

class WiFiManager {
public:
    using StatusCallback = void (*)(WiFiStatus, const String& detail);
    using ConfigChangedCallback = std::function<void()>;

    // main.cpp owns the config document and its persistence; WiFiManager
    // just reads/writes its own "wifi" section within it and asks to be
    // saved via onChanged whenever that section changes.
    void setConfig(JsonDocument& doc, ConfigChangedCallback onChanged = nullptr) {
        _config    = &doc;
        _onChanged = std::move(onChanged);
    }

    // A recursive mutex guarding the SD card, held only while a response is
    // actually being written. It is deliberately not held across the upload and
    // the wait for the backend, so the background photo writer has a real gap
    // to work in instead of just being deferred to the end.
    void setSdLock(SemaphoreHandle_t lock) { _sdLock = lock; }

    static bool loadNetworksFromJson(JsonDocument& doc, String ssids[], String passes[], uint32_t& count);
    static bool saveNetworksToJson(JsonDocument& doc, const String ssids[], const String passes[], uint32_t count);

    // Try all saved networks; reports Disconnected if none succeed
    void begin(uint32_t timeoutPerNetworkMs = 5000);

    // Called from loop() — monitors connection and handles reconnects
    void loop();

    void onStatusChange(StatusCallback cb) { _statusCallback = cb; }

    // Fired once when the board has given up on getting online, either because
    // the retry budget ran out or because something asked for the network while
    // it was down. Cleared when a connection is established, so a later outage
    // fires again.
    //
    // It can be called from inside a request, so the handler must not block or
    // touch the card or the amplifier — set a flag and act from loop().
    using OfflineCallback = std::function<void(const char* why)>;
    void onOffline(OfflineCallback cb) { _onOffline = std::move(cb); }

    // Full sweeps of every saved network to try before calling it offline.
    // 0 retries for ever, which is the old behaviour.
    void setMaxReconnectSweeps(uint32_t sweeps) { _maxSweeps = sweeps; }

    bool   isConnected() const;
    String localIP() const;

    // Called by BLE when the app sends credentials
    void addNetwork(const String& ssid, const String& password);
    void removeNetwork(const String& ssid);

    // Text -> whole 16-bit 16 kHz mono WAV, or "" on failure. Has 0x00 bytes
    // in it, so use length(), never c_str().
    String textToSpeech(const String text); //WAV

    // Same, for a fixed clip asked by name ("no_wifi", "start_talking"...).
    // The backend holds what it says; the device only knows the name.
    String defaultAudio(const String& clipId); //WAV

    // The same clip, written straight to an open file instead of returned.
    //
    // Prefer this: the String above is capped by internal heap, which PSRAM
    // does not extend, and the longer clips do not fit. first_boot is 391 KB
    // against a largest free block around 120 KB, so it could never arrive.
    bool defaultAudioToFile(const String& clipId, File& out);

    // Sends a JPEG to /look and writes the spoken description into `out` as a
    // WAV, ready for Audio::playWav(). The text the backend chose is returned
    // through `spokenText` when one is passed.
    //
    // True only if a complete, RIFF-headed file was written; on false `out` may
    // hold a partial one, so the caller should remove it.
    //
    // Kept for anything that genuinely wants the answer on disk. For speaking
    // it, use lookStreaming(): this one cannot make a sound until the last byte
    // has landed, which on a phone hotspot is seconds of silence.
    bool look(const uint8_t* jpeg, size_t jpegLen, File& out,
              String* spokenText = nullptr);

    // Where the audio goes when it is played as it arrives. onStart gets the
    // sample rate the backend chose, then onChunk gets raw pcm16 in whatever
    // sizes the network delivers. Either may return false to abort.
    struct AudioSink {
        std::function<bool(uint32_t sampleRate)>          onStart;
        std::function<bool(const uint8_t*, size_t)>       onChunk;
    };

    // Sends a JPEG to /look and pushes the reply at the sink as it arrives, so
    // the speaker starts while the rest is still downloading. Asks for pcm16 —
    // raw signed 16-bit samples, which is what the amplifier wants and what the
    // backend produces natively, with no WAV header and no file in between.
    bool lookStreaming(const uint8_t* jpeg, size_t jpegLen,
                       const AudioSink& sink, String* spokenText = nullptr);

private:
    StatusCallback _statusCallback = nullptr;

    JsonDocument*         _config    = nullptr;
    ConfigChangedCallback _onChanged;
    SemaphoreHandle_t     _sdLock    = nullptr;

    // Kept alive between requests rather than built per call, so the TLS
    // handshake is paid once instead of on every button press. That handshake
    // costs well over a second on this chip: verifying the chain against the
    // root bundle is the single most expensive thing in a /look.
    //
    // The certificate bundle is also attached only once — reattaching it per
    // request was throwing away the very thing being reused.
    WiFiClient       _plain;
    WiFiClientSecure _tls;
    HTTPClient       _http;
    bool             _tlsReady = false;

    uint32_t _lastCheckMs       = 0;
    uint32_t _timeoutPerNetwork = 5000;

    // --- non-blocking reconnect ---------------------------------------------
    //
    // loop() used to run a whole blocking sweep: _trySTA() sat in a
    // while/delay(100) for up to _timeoutPerNetwork per network, so a board that
    // could not reach its network froze the Arduino loop for five seconds at a
    // time. The button is polled by that loop, so pressing it did nothing for
    // seconds. Now loop() only ever advances this small machine and returns.
    //
    // begin() is still blocking on purpose: at boot there is nothing else to do
    // and the board should get its best shot at connecting before setup() goes
    // on to things that need the network.
    enum class Phase : uint8_t {
        Idle,        // connected, or waiting out the gap before the next sweep
        Settling,    // disconnect() issued, letting the driver leave "connecting"
        Connecting,  // begin() issued for _netIndex, waiting on the deadline
    };

    Phase    _phase          = Phase::Idle;
    uint32_t _phaseStartMs   = 0;
    uint32_t _netIndex       = 0;
    uint32_t _netCount       = 0;
    String   _sweepSsids[WIFI_MAX_NETWORKS];
    String   _sweepPasses[WIFI_MAX_NETWORKS];

    OfflineCallback _onOffline;
    uint32_t        _maxSweeps    = 3;
    uint32_t        _failedSweeps = 0;
    bool            _offlineFired = false;

    bool _loadSweep();          // reads the saved networks into _sweep*
    void _beginAttempt();       // begin() for _netIndex, enters Connecting
    void _sweepFailed();        // one full pass over every network came back empty
    void _goOffline(const char* why);

    bool _tryAllNetworks();
    bool _trySTA(const char* ssid, const char* password);
    void _notify(WiFiStatus status, const String& detail = "");

    // Connection, token, limits and WAV check for the calls above.
    // `ruta` is everything after backend_url; `qui` only labels Serial output.
    String _getAudio(const String& ruta, const char* qui);

    // The one place a request is actually made. Exactly one of outStr/outFile
    // must be set: a String for short clips, or the card for anything that
    // would not fit in internal heap. A body turns it into a POST.
    bool _request(const String& ruta, const char* qui,
                  const uint8_t* body, size_t bodyLen,
                  String* outStr, File* outFile, const AudioSink* sink,
                  String* textHeader);

    // Builds the /look JSON body in PSRAM and runs the request. Shared by
    // look() and lookStreaming(), which differ only in the format they ask for
    // and where the reply goes.
    bool _look(const uint8_t* jpeg, size_t jpegLen, const char* audioFormat,
               File* out, const AudioSink* sink, String* spokenText);
};
