#include "Audio.h"
#include <Arduino.h>
#include <SD.h>
#include <WiFiManager.h>
#include <Lang.h>
#include <driver/i2s.h>

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

constexpr const char*  kClipsDir  = "/audio";
constexpr i2s_port_t   kI2sPort   = I2S_NUM_0;
constexpr uint32_t     kStartRate = 16000;

// 512 in + 1024 out is 3 KB of stack. The loopTask only gets 8 KB, so bigger
// buffers here are a stack overflow waiting to happen.
constexpr size_t kBufferSamples = 512;

String pathOf(const char* name, const String& lang) {
    return String(kClipsDir) + "/" + name + "_" + lang + ".wav";
}

// RIFF chunks are word-aligned: an odd size is followed by a pad byte.
uint32_t padded(uint32_t chunkSize) {
    return chunkSize + (chunkSize & 1);
}

struct WavInfo {
    uint32_t sampleRate = 0;
    uint16_t channels   = 0;
    uint32_t dataStart  = 0;
    uint32_t dataSize   = 0;
};

// Reads the header and leaves the file positioned anywhere; playWav seeks.
bool readWavHeader(File& file, WavInfo& out) {
    char riff[4], wave[4];
    if (file.read((uint8_t*)riff, 4) != 4) return false;
    file.seek(8);
    if (file.read((uint8_t*)wave, 4) != 4) return false;
    if (strncmp(riff, "RIFF", 4) != 0 || strncmp(wave, "WAVE", 4) != 0) {
        Serial.println("Not a RIFF/WAVE file");
        return false;
    }

    uint16_t bitsPerSample = 0;

    while (file.available()) {
        char     chunkID[4];
        uint32_t chunkSize;
        if (file.read((uint8_t*)chunkID, 4) != 4) break;
        if (file.read((uint8_t*)&chunkSize, 4) != 4) break;

        if (strncmp(chunkID, "fmt ", 4) == 0) {
            uint16_t audioFormat, blockAlign;
            uint32_t byteRate;
            file.read((uint8_t*)&audioFormat, 2);
            file.read((uint8_t*)&out.channels, 2);
            file.read((uint8_t*)&out.sampleRate, 4);
            file.read((uint8_t*)&byteRate, 4);
            file.read((uint8_t*)&blockAlign, 2);
            file.read((uint8_t*)&bitsPerSample, 2);
            if (chunkSize > 16) file.seek(file.position() + padded(chunkSize) - 16);

            if (audioFormat != 1 || bitsPerSample != 16) {
                Serial.println("Unsupported WAV: only 16-bit PCM");
                return false;
            }
        } else if (strncmp(chunkID, "data", 4) == 0) {
            out.dataStart = file.position();
            // A header claiming more than the file holds (0xFFFFFFFF from a
            // streaming writer) would otherwise loop until the read runs dry.
            const uint32_t available = file.size() - out.dataStart;
            out.dataSize = chunkSize < available ? chunkSize : available;
            break;
        } else {
            file.seek(file.position() + padded(chunkSize));
        }
    }

    if (out.dataSize == 0 || out.sampleRate == 0) {
        Serial.println("WAV has no data chunk");
        return false;
    }
    if (out.channels != 1) {
        Serial.printf("Expected mono, got %u channels\n", out.channels);
        return false;
    }
    return true;
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

bool Audio::begin() {
    pinMode(kAmpEnable, OUTPUT);
    digitalWrite(kAmpEnable, LOW);   // amplifier off until there is audio

    const i2s_config_t config = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = kStartRate,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 256,
        .use_apll             = false,
        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 0,
    };
    const i2s_pin_config_t pins = {
        .mck_io_num   = I2S_PIN_NO_CHANGE,
        .bck_io_num   = kI2sBclk,
        .ws_io_num    = kI2sLrc,
        .data_out_num = kI2sDin,
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };

    esp_err_t err = i2s_driver_install(kI2sPort, &config, 0, nullptr);
    if (err != ESP_OK) {
        Serial.printf("i2s_driver_install failed: %s\n", esp_err_to_name(err));
        return false;
    }
    err = i2s_set_pin(kI2sPort, &pins);
    if (err != ESP_OK) {
        Serial.printf("i2s_set_pin failed: %s\n", esp_err_to_name(err));
        i2s_driver_uninstall(kI2sPort);
        return false;
    }

    _ready = true;
    return true;
}

bool Audio::playWav(const char* path) {
    if (!_ready) {
        Serial.println("Audio::begin() has not run");
        return false;
    }

    File file = SD.open(path, FILE_READ);
    if (!file) {
        Serial.printf("Failed to open %s\n", path);
        return false;
    }

    WavInfo wav;
    if (!readWavHeader(file, wav)) {
        file.close();
        return false;
    }

    // The clips are 16 kHz but /speak also serves 8000 and 22050, so the rate
    // comes from the file instead of being hardcoded.
    i2s_set_clk(kI2sPort, wav.sampleRate, I2S_BITS_PER_SAMPLE_16BIT,
                I2S_CHANNEL_STEREO);

    file.seek(wav.dataStart);
    _playingAudio = true;
    digitalWrite(kAmpEnable, HIGH);

    int16_t  input[kBufferSamples];
    int16_t  output[kBufferSamples * 2];
    uint32_t remaining = wav.dataSize;

    while (remaining > 0) {
        const size_t toRead = min((uint32_t)kBufferSamples, remaining / 2);
        if (toRead == 0) break;

        const size_t bytesRead =
            file.read((uint8_t*)input, toRead * sizeof(int16_t));
        if (bytesRead == 0) break;

        const size_t samples = bytesRead / sizeof(int16_t);
        // Audio in the LEFT slot only: that is the channel the MAX98357A picks
        // with SD driven high.
        for (size_t i = 0; i < samples; i++) {
            output[i * 2]     = input[i];
            output[i * 2 + 1] = 0;
        }

        size_t written = 0;
        i2s_write(kI2sPort, output, samples * 2 * sizeof(int16_t), &written,
                  portMAX_DELAY);
        remaining -= bytesRead;
    }

    // Let the DMA buffers drain before cutting the amplifier, or the tail of
    // the sentence gets clipped.
    i2s_zero_dma_buffer(kI2sPort);
    digitalWrite(kAmpEnable, LOW);
    _playingAudio = false;

    file.close();
    return true;
}

bool Audio::playDefault(DefaultAudios audio) {
    const String path = pathFor(audio, bonsai::lang());
    if (path.length() == 0) return false;
    if (!SD.exists(path)) {
        Serial.printf("%s is not on the SD card yet\n", path.c_str());
        return false;
    }
    return playWav(path.c_str());
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
