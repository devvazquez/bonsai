#pragma once
#include <Arduino.h>
#include <driver/i2s_std.h>

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
// U2 pad 4, so SD_MODE sits directly on the card's data-out line. Driving it as
// a push-pull output clamps MISO and the card can never answer again, so the
// pin is only ever taken while nothing is reading the card. The rest of the
// time it is left as an input, where SD_MODE's internal 100k pull-down takes it
// to ground and shuts the amplifier down on its own — no external part needed,
// and only ~33 uA of load on MISO, which SPI does not notice.
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

    // Plays a default clip in the current language, if it is on the SD card.
    bool playDefault(DefaultAudios audio);

    bool isPlaying() const { return _playingAudio; }

    // Downloads every clip that is not on the SD card yet, to
    // /audio/<clip>_<lang>.wav.
    void downloadDefaultAudiosIfMissing(WiFiManager& wifi);

    // The name the backend knows this clip by, or "" if there is none.
    static const char* nameFor(DefaultAudios audio);

    // Where a clip's WAV lives (or will live) on the SD card.
    static String pathFor(DefaultAudios audio, const String& lang);

private:
    // Takes kAmpEnable and drives it high, waking the amplifier in left-slot
    // mode. Only safe with no SD access in flight — see the note on kAmpEnable.
    void ampTake();

    // Hands kAmpEnable back to the SPI bus so the microSD can be read again.
    void ampRelease();

    i2s_chan_handle_t _tx = nullptr;
    bool _playingAudio    = false;
};
