#pragma once
#include <Arduino.h>

// Forward declaration, not #include <WiFiManager.h>: the two headers would
// include each other. Audio.cpp includes the full header.
class WiFiManager;

// The default clips: enum id and the name the backend knows them by. Adding one
// here is enough — the enum and the download loop are both generated from it.
// What each one says lives in the backend: GET /api/v1/clips?lang=ca
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
    void playDefault(DefaultAudios audio);

    // Downloads every clip that is not on the SD card yet, to
    // /audio/<clip>_<lang>.wav.
    void downloadDefaultAudiosIfMissing(WiFiManager& wifi);

    // The name the backend knows this clip by, or "" if there is none.
    static const char* nameFor(DefaultAudios audio);

    // Where a clip's WAV lives (or will live) on the SD card.
    static String pathFor(DefaultAudios audio, const String& lang);

private:
    bool _playingAudio = false;
};
