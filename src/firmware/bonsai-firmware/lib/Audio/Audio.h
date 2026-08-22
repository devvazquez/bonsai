#pragma once
#include <Arduino.h>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <freertos/semphr.h>

// Forward declaration, not #include <WiFiManager.h>: the two headers would
// include each other. Audio.cpp includes the full header.
class WiFiManager;

// Pins, from the schematic (U1 = XIAO ESP32-S3, U2 = MAX98357A).
// DIN=U1/1 D0, BLCK=U1/2 D1, LRC=U1/3 D2, SD=U1/10 D9.
constexpr int kI2sDin  = 1;   // amplifier data in
constexpr int kI2sBclk = 2;   // bit clock
constexpr int kI2sLrc  = 3;   // word select
// SD_MODE on the MAX98357A (the bare part, U2 — not a breakout). Above 1.4 V it
// plays the LEFT slot, which is why playWav() puts the samples there and zeroes
// the right one; below 0.16 V the amplifier is shut down.
//
// GPIO8 is also D9 = MISO of the SPI bus the microSD slot on the Sense board
// hangs off, and this net carries no resistor — U1 pad 10 runs straight to
// U2 pad 4, so SD_MODE sits directly on the card's data-out line.
//
// This pin is the whole reason the card used to fail. What matters is not the
// level it is left at but WHO OWNS IT: SPI.begin() routes the pad to the SPI
// peripheral's MISO input, and any pinMode() call on it — INPUT and
// INPUT_PULLUP just as much as OUTPUT — points it back at plain GPIO and leaves
// the peripheral reading nothing. The card then mounts, because identification
// is short and forgiving, and every transfer afterwards comes back empty.
// SD_MODE's internal 100k pull-down is a red herring; it never disturbed the
// bus, and no pull setting rescues a pin the peripheral can no longer see.
//
// So: nothing outside playWav() touches this pin, and playWav() only takes it
// once the clip is in PSRAM and the file is closed, then gives it straight back
// to the SPI peripheral. Between clips the pull-down holds SD_MODE at ground,
// which shuts the amplifier down at no cost.
constexpr int kAmpEnable = 8;

// The default clips: enum id and the name the backend knows them by. Adding one
// here is enough — the enum and the download loop are both generated from it.
// What each one says lives in the backend: GET /api/v1/audios?lang=ca
#define BONSAI_DEFAULT_AUDIOS(X)                                              \
    X(NO_WIFI,        "no_wifi")        /* the WiFi dropped */                \
    X(START_TALKING,  "start_talking")  /* plays while the photo uploads */   \
    X(FIRST_BOOT,     "first_boot")     /* first-boot introduction */         \
    X(MISSING_CONFIG, "missing_config") /* something missing from config */

enum class DefaultAudios {
#define BONSAI_ENUM_ENTRY(id, name) id,
    BONSAI_DEFAULT_AUDIOS(BONSAI_ENUM_ENTRY)
#undef BONSAI_ENUM_ENTRY
};

class Audio {
public:
    // Installs the I2S driver. Call once from setup(), before playing anything.
    bool begin();

    // Plays a mono 16-bit PCM WAV from the SD card, blocking until it ends.
    // Any sample rate the file declares; false if it cannot be played.
    bool playWav(const char* path);

    // --- Playing straight off the network ---------------------------------
    //
    // For /look, where waiting for the whole sentence to download before making
    // any sound doubles the delay for no reason: the backend emits raw pcm16 as
    // soon as the TTS has a first sentence, and these push it at the amplifier
    // as it lands. Nothing is buffered and the card is never touched.
    //
    // beginStream, then writeStream as many times as there is data, then
    // endStream — which must be called even on failure, since beginStream takes
    // the amplifier's pin and only endStream gives it back.
    //
    // writeStream only copies into a jitter buffer; a separate task does the
    // talking to I2S. That separation is the point. i2s_channel_write() blocks
    // until the DMA has room, i.e. in real time, and doing that on the thread
    // that reads the socket means not reading the socket — the TCP window
    // closes, the backend stops, and the audio arrives in bursts with holes
    // between them. Measured before the split: 6 s of speech took 18 s.
    bool   beginStream(uint32_t sampleRate);
    size_t writeStream(const uint8_t* pcm, size_t len);
    void   endStream();

    // Plays a default clip in the current language, if it is on the SD card.
    bool playDefault(DefaultAudios audio);

    // A plain sine out of the speaker. Synthesised, so it needs nothing on the
    // card and nothing from the backend — which is the whole point: the long
    // press that opens the setup access point has to be able to acknowledge
    // itself on a board with no clips downloaded yet, or no card at all.
    // Blocks for `ms` and takes GPIO8 for that long, so no SD access meanwhile.
    bool beep(uint32_t freqHz, uint32_t ms);

    bool isPlaying() const { return _playingAudio; }

    // Downloads every clip that is not on the SD card yet, to
    // /audio/<clip>_<lang>.wav.
    void downloadDefaultAudiosIfMissing(WiFiManager& wifi);

    // The name the backend knows this clip by, or "" if there is none.
    static const char* nameFor(DefaultAudios audio);

    // Where a clip's WAV lives (or will live) on the SD card.
    static String pathFor(DefaultAudios audio, const String& lang);

private:
    // Takes kAmpEnable off the SPI peripheral and drives it high, waking the
    // amplifier in left-slot mode. Only safe with no SD access in flight, and
    // every call must be paired with ampRelease() — see the note on kAmpEnable.
    static void ampTake();

    // Gives kAmpEnable back to the SPI peripheral so the card works again.
    static void ampRelease();

    i2s_chan_handle_t _tx = nullptr;
    bool _playingAudio    = false;

    // A network chunk can end mid-sample. The odd byte waits here for the next
    // chunk instead of being dropped, which would otherwise swap the halves of
    // every sample after it and turn the rest of the sentence into noise.
    uint8_t _carry    = 0;
    bool    _hasCarry = false;

    static void _playerTrampoline(void* self);
    void        _playerLoop();

    StreamBufferHandle_t _ring    = nullptr;   // network -> player

    // The ring's storage lives in PSRAM, so growing it to hold a two second
    // prebuffer does not come out of the internal heap that WiFi and TLS need.
    // Only the small control block is internal. Legal because both ends of this
    // buffer are tasks — PSRAM must not be touched from an ISR, and nothing here
    // does.
    StaticStreamBuffer_t _ringCtl{};
    uint8_t*             _ringStore = nullptr;
    TaskHandle_t         _player  = nullptr;
    SemaphoreHandle_t    _drained = nullptr;   // player says the ring is empty
    volatile bool        _ending  = false;

    uint32_t _streamRate = 16000;

    // How often the player wanted samples and the buffer was empty. Reported by
    // endStream(): a non-zero count is choppy audio, and it is the only way to
    // measure that without listening to the speaker.
    volatile uint32_t _underruns = 0;
    volatile size_t   _played    = 0;
};
