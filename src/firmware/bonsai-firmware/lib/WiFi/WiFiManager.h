#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
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

    static bool loadNetworksFromJson(JsonDocument& doc, String ssids[], String passes[], uint32_t& count);
    static bool saveNetworksToJson(JsonDocument& doc, const String ssids[], const String passes[], uint32_t count);

    // Try all saved networks; reports Disconnected if none succeed
    void begin(uint32_t timeoutPerNetworkMs = 5000);

    // Called from loop() — monitors connection and handles reconnects
    void loop();

    void onStatusChange(StatusCallback cb) { _statusCallback = cb; }

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

private:
    StatusCallback _statusCallback = nullptr;

    JsonDocument*         _config    = nullptr;
    ConfigChangedCallback _onChanged;

    uint32_t _lastCheckMs       = 0;
    uint32_t _timeoutPerNetwork = 5000;

    bool _tryAllNetworks();
    bool _trySTA(const char* ssid, const char* password);
    void _notify(WiFiStatus status, const String& detail = "");

    // Connection, token, limits and WAV check for the two calls above.
    // `ruta` is everything after backend_url; `qui` only labels Serial output.
    String _getAudio(const String& ruta, const char* qui);
};
