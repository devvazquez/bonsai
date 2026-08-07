#include "Audio.h"
#include <Arduino.h>
#include <SD.h>
#include <WiFiManager.h>
#include <Lang.h>

namespace {

struct DefaultClip {
    DefaultAudios id;
    const char*   name;
};

// Generated from the list in Audio.h: nothing to add here for a new clip.
const DefaultClip kClips[] = {
#define BONSAI_CLIP_ENTRY(id, name) { DefaultAudios::id, name },
    BONSAI_DEFAULT_AUDIOS(BONSAI_CLIP_ENTRY)
#undef BONSAI_CLIP_ENTRY
};

constexpr const char* kClipsDir = "/audio";

String pathOf(const char* name, const String& lang) {
    return String(kClipsDir) + "/" + name + "_" + lang + ".wav";
}

}  // namespace

const char* Audio::nameFor(DefaultAudios audio) {
    for (const DefaultClip& clip : kClips) {
        if (clip.id == audio) return clip.name;
    }
    return "";
}

String Audio::pathFor(DefaultAudios audio, const String& lang) {
    const char* name = nameFor(audio);
    return name[0] ? pathOf(name, lang) : String("");
}

void Audio::playDefault(DefaultAudios audio) {
    Serial.print("Playing default audio: ");
    Serial.println(nameFor(audio));
}

void Audio::downloadDefaultAudiosIfMissing(WiFiManager& wifi) {
    const String lang = bonsai::lang();

    if (!SD.exists(kClipsDir) && !SD.mkdir(kClipsDir)) {
        Serial.println("Could not create /audio on the SD card.");
        return;
    }

    uint32_t downloaded = 0, failed = 0, present = 0;

    for (const DefaultClip& clip : kClips) {
        // The language is in the filename, so setLang() re-downloads them
        // instead of playing the old language.
        const String path = pathOf(clip.name, lang);

        if (SD.exists(path)) {
            ++present;
            continue;
        }

        // No WiFi is not an error here: they get downloaded on a later boot.
        if (!wifi.isConnected()) {
            Serial.printf("%s is missing and there is no WiFi: left for later.\n",
                          path.c_str());
            ++failed;
            continue;
        }

        Serial.printf("Downloading %s...\n", path.c_str());
        const String wav = wifi.defaultAudio(clip.name);
        if (wav.length() == 0) {
            Serial.println("  ...nothing came back from the backend.");
            ++failed;
            continue;
        }

        File file = SD.open(path, FILE_WRITE);
        if (!file) {
            Serial.println("  ...could not open the file on the SD card.");
            ++failed;
            continue;
        }

        // length(), not c_str(): the WAV has 0x00 bytes in it.
        const size_t written = file.write(
            reinterpret_cast<const uint8_t*>(wav.c_str()), wav.length());
        file.close();

        if (written != wav.length()) {
            // A half file is worse than none: SD.exists() would accept it later.
            Serial.printf("  ...only %u of %u bytes were written; removing it.\n",
                          written, wav.length());
            SD.remove(path);
            ++failed;
            continue;
        }

        Serial.printf("  ...%u bytes.\n", wav.length());
        ++downloaded;
    }

    Serial.printf("Clips in %s: %u already there, %u downloaded, %u pending.\n",
                  lang.c_str(), present, downloaded, failed);
}
