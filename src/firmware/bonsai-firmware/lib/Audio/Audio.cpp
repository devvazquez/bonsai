#include "Audio.h"
#include <Arduino.h>
#include <SD.h>
#include <WiFiManager.h>
#include <Lang.h>
#include <SPI.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>

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

constexpr const char* kClipsDir  = "/audio";
constexpr uint32_t    kStartRate = 16000;

// 512 in + 1024 out is 3 KB of stack. The loopTask only gets 8 KB, so bigger
// buffers here are a stack overflow waiting to happen.
constexpr size_t kBufferSamples = 512;

// The whole clip is buffered in PSRAM before playback starts, so this is the
// longest one that can be played: 4 MB is ~130 s of the 16 kHz mono the
// backend serves, well past anything /speak returns, and leaves most of the
// 8 MB free.
constexpr size_t kMaxClipBytes = 4 * 1024 * 1024;

// The jitter buffer between the socket and the DMA: four seconds at 8 kHz.
// It only needs to be big enough to hold the prebuffer plus whatever arrives
// while a burst is being spoken.
constexpr size_t   kRingBytes           = 64 * 1024;

// How long to collect before the first sound, as a fraction of real time rather
// than a byte count, so it means the same thing at 8 kHz and at 16.
//
// This is the one number that trades latency for not stuttering. The link
// measured 21-30 KB/s against the 16 KB/s that 8 kHz consumes, so it is ahead
// on average but only by a little, and TLS delivers in records rather than
// evenly — a gap of a few hundred ms is normal and has to be absorbed here or
// it is audible.
constexpr uint32_t kPrebufferMs         = 1000;
constexpr uint32_t kPrebufferTimeoutMs  = 2500;

// Silence pushed after the last sample so the DMA plays out what it is still
// holding before the amplifier is cut. The default I2S config holds about
// 180 ms at 8 kHz, and cutting into that clips the last word.
constexpr uint32_t kDrainMs             = 400;

// Upper bound on waiting for buffered audio to finish. The amplifier pin is
// GPIO8, the card's MISO, so this must not be able to hang for ever.
constexpr uint32_t kDrainTimeoutMs      = 30000;

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

void Audio::ampTake() {
    pinMode(kAmpEnable, OUTPUT);
    digitalWrite(kAmpEnable, HIGH);
}

void Audio::ampRelease() {
    // Letting go is not enough: the pinMode() in ampTake() moved this pad from
    // the SPI peripheral's MISO input to plain GPIO, and dropping it back to
    // INPUT leaves it there. The peripheral has to be pointed at the pad again,
    // the same way SPI.begin() did it in the first place, or the card answers
    // into a disconnected input from here on.
    pinMode(kAmpEnable, INPUT);
    spiAttachMISO(SPI.bus(), kAmpEnable);
}

bool Audio::begin() {
    // Deliberately does NOT touch kAmpEnable. That pin is the card's MISO, and
    // calling pinMode() on a pin the SPI peripheral is driving takes the line
    // away from it. Only playWav() borrows it, once the file is closed.

    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &_tx, nullptr);
    if (err != ESP_OK) {
        Serial.printf("i2s_new_channel failed: %s\n", esp_err_to_name(err));
        return false;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(kStartRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)kI2sBclk,
            .ws   = (gpio_num_t)kI2sLrc,
            .dout = (gpio_num_t)kI2sDin,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    err = i2s_channel_init_std_mode(_tx, &std_cfg);
    if (err != ESP_OK) {
        Serial.printf("i2s_channel_init_std_mode failed: %s\n", esp_err_to_name(err));
        i2s_del_channel(_tx);
        _tx = nullptr;
        return false;
    }

    i2s_channel_enable(_tx);
    return true;
}

void Audio::_playerTrampoline(void* self) {
    static_cast<Audio*>(self)->_playerLoop();
}

// The consumer end of the jitter buffer. Owns its own stack, so the staging
// buffer here is not competing with mbedTLS the way it would inside the
// download callback.
void Audio::_playerLoop() {
    constexpr size_t kStageSamples = 256;
    int16_t stereo[kStageSamples * 2];
    uint8_t mono[kStageSamples * sizeof(int16_t)];

    for (;;) {
        // Parked until beginStream() says there is a sentence coming.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Let the buffer fill before making a sound. Starting on the first byte
        // guarantees an underrun a moment later, because the network delivers in
        // bursts and the DMA drains at a constant rate.
        const size_t   target = (size_t)_streamRate * 2 * kPrebufferMs / 1000;
        const uint32_t tWait  = millis();
        while (!_ending
               && xStreamBufferBytesAvailable(_ring) < target
               && millis() - tWait < kPrebufferTimeoutMs) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        _underruns = 0;
        _played    = 0;

        for (;;) {
            const size_t got = xStreamBufferReceive(_ring, mono, sizeof(mono),
                                                    pdMS_TO_TICKS(100));
            if (got == 0) {
                // Nothing waiting: either the sentence is over, or the network
                // is behind and the DMA is about to run dry anyway.
                if (_ending && xStreamBufferBytesAvailable(_ring) == 0) break;
                // Wanted samples and had none: that is a hole in the audio, and
                // counting them is the only way to tell a smooth stream from a
                // choppy one without standing next to the speaker.
                ++_underruns;
                continue;
            }
            _played += got;

            const size_t samples = got / sizeof(int16_t);
            const int16_t* in = reinterpret_cast<const int16_t*>(mono);
            for (size_t i = 0; i < samples; ++i) {
                // Left slot only, right zeroed: the channel the MAX98357A picks
                // with SD_MODE high, same as playWav().
                stereo[i * 2]     = in[i];
                stereo[i * 2 + 1] = 0;
            }
            size_t written = 0;
            i2s_channel_write(_tx, stereo, samples * 2 * sizeof(int16_t),
                              &written, portMAX_DELAY);
        }

        xSemaphoreGive(_drained);
    }
}

bool Audio::beginStream(uint32_t sampleRate) {
    if (!_tx) {
        Serial.println("Audio::begin() has not run");
        return false;
    }
    if (sampleRate == 0) sampleRate = kStartRate;

    if (!_ring) {
        _ring = xStreamBufferCreate(kRingBytes, 1);
        if (!_ring) {
            Serial.printf("No memory for a %u byte audio buffer\n", kRingBytes);
            return false;
        }
    }
    if (!_drained) _drained = xSemaphoreCreateBinary();
    if (!_player) {
        // Core 1, next to the loop task that feeds it, and above it in priority
        // so a late chunk is turned into sound the moment it lands.
        xTaskCreatePinnedToCore(_playerTrampoline, "i2splay", 4096, this, 2,
                                &_player, 1);
        if (!_player) {
            Serial.println("Could not start the audio player task");
            return false;
        }
    }

    xStreamBufferReset(_ring);
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate);
    i2s_channel_disable(_tx);
    i2s_channel_reconfig_std_clock(_tx, &clk_cfg);
    i2s_channel_enable(_tx);

    _streamRate   = sampleRate;
    _hasCarry     = false;
    _ending       = false;
    _playingAudio = true;
    ampTake();

    xTaskNotifyGive(_player);
    return true;
}

// Copy in, return at once. Blocks only if the buffer is genuinely full, which
// means the network is ahead of real time and waiting is exactly right.
size_t Audio::writeStream(const uint8_t* data, size_t len) {
    if (!_ring || !data || len == 0) return 0;

    size_t idx = 0;
    size_t sent = 0;

    // The odd byte from a chunk boundary goes in first, so what reaches the ring
    // is always whole little-endian samples.
    if (_hasCarry && idx < len) {
        const uint8_t pair[2] = { _carry, data[idx++] };
        sent += xStreamBufferSend(_ring, pair, 2, pdMS_TO_TICKS(1000));
        _hasCarry = false;
    }

    const size_t whole = ((len - idx) / 2) * 2;
    if (whole > 0) {
        sent += xStreamBufferSend(_ring, data + idx, whole, pdMS_TO_TICKS(1000));
        idx += whole;
    }
    if (idx < len) {
        _carry    = data[idx];
        _hasCarry = true;
    }
    return sent;
}

void Audio::endStream() {
    if (!_tx) return;

    if (_player && _ring) {
        _ending = true;
        // Wait for what is already buffered to be spoken. Bounded, so a stuck
        // player cannot hold the amplifier pin — and with it the card's MISO.
        if (_drained) xSemaphoreTake(_drained, pdMS_TO_TICKS(kDrainTimeoutMs));
    }

    // Silence so the DMA drains before the amplifier is cut, or the tail of the
    // sentence is clipped. Sized from the rate rather than a fixed number of
    // buffers, which at 8 kHz came to barely more than the DMA holds.
    int16_t quiet[kBufferSamples * 2];
    memset(quiet, 0, sizeof(quiet));
    const size_t drainSamples = (size_t)_streamRate * kDrainMs / 1000;
    for (size_t done = 0; done < drainSamples; done += kBufferSamples) {
        size_t written = 0;
        i2s_channel_write(_tx, quiet, sizeof(quiet), &written, portMAX_DELAY);
    }

    ampRelease();
    _playingAudio = false;
    _hasCarry     = false;
    _ending       = false;

    Serial.printf("  [t] audio: %u KB played at %u Hz (%u ms), %u underruns\n",
                  (unsigned)(_played / 1024), _streamRate,
                  _streamRate ? (unsigned)(_played * 1000 / (_streamRate * 2)) : 0u,
                  _underruns);
}

bool Audio::playWav(const char* path) {
    if (!_tx) {
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

    if (wav.dataSize > kMaxClipBytes) {
        Serial.printf("%s holds %u bytes of audio, more than the %u that fit\n",
                      path, wav.dataSize, (uint32_t)kMaxClipBytes);
        file.close();
        return false;
    }

    // The clip is pulled into PSRAM in one go and the file closed before a
    // single sample goes out. Enabling the amplifier means driving GPIO8, which
    // is the same line the card answers on, so reading and playing at the same
    // time is not something this board can do — see the note on kAmpEnable.
    int16_t* clip =
        (int16_t*)heap_caps_malloc(wav.dataSize, MALLOC_CAP_SPIRAM);
    if (!clip) {
        Serial.printf("No PSRAM for %u bytes of audio\n", wav.dataSize);
        file.close();
        return false;
    }

    const uint32_t tLoadStart = millis();
    file.seek(wav.dataStart);
    const size_t bytesRead = file.read((uint8_t*)clip, wav.dataSize);
    file.close();
    const uint32_t loadMs = millis() - tLoadStart;

    if (bytesRead == 0) {
        Serial.printf("Read nothing back from %s\n", path);
        heap_caps_free(clip);
        return false;
    }

    // The clips are 16 kHz but /speak also serves 8000 and 22050, so the rate
    // comes from the file instead of being hardcoded.
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(wav.sampleRate);
    i2s_channel_disable(_tx);
    i2s_channel_reconfig_std_clock(_tx, &clk_cfg);
    i2s_channel_enable(_tx);

    _playingAudio = true;
    ampTake();

    const uint32_t tPlayStart = millis();
    int16_t      output[kBufferSamples * 2];
    const size_t total = bytesRead / sizeof(int16_t);

    for (size_t done = 0; done < total; done += kBufferSamples) {
        const size_t samples = min(kBufferSamples, total - done);

        // Audio in the LEFT slot only: that is the channel the MAX98357A picks
        // with SD driven high.
        for (size_t i = 0; i < samples; i++) {
            output[i * 2]     = clip[done + i];
            output[i * 2 + 1] = 0;
        }

        size_t written = 0;
        i2s_channel_write(_tx, output, samples * 2 * sizeof(int16_t), &written,
                          portMAX_DELAY);
    }

    // Push silence through so the DMA buffers drain before the amplifier is
    // cut, or the tail of the sentence gets clipped.
    memset(output, 0, sizeof(output));
    for (int i = 0; i < 3; i++) {
        size_t written = 0;
        i2s_channel_write(_tx, output, sizeof(output), &written, portMAX_DELAY);
    }
    ampRelease();
    _playingAudio = false;

    // Split on purpose: "load" is the card, "play" is the clip's own duration.
    // Only the first is worth optimising — the second is how long the sentence
    // takes to say, and no amount of tuning shortens that.
    const uint32_t playMs = millis() - tPlayStart;
    Serial.printf("  [t] playWav %s: load %u ms (%u KB at %u KB/s), play %u ms\n",
                  path, loadMs, (unsigned)(bytesRead / 1024),
                  loadMs ? (unsigned)(bytesRead / loadMs) : 0u, playMs);

    heap_caps_free(clip);
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

        File file = SD.open(path, FILE_WRITE);
        if (!file) {
            Serial.println("  ...could not open the file on the SD card.");
            ++failed;
            continue;
        }

        // Streamed rather than fetched into a String: these clips run to
        // hundreds of KB and a String has only internal heap to live in, so the
        // longer ones never fitted and simply never downloaded.
        const bool ok = wifi.defaultAudioToFile(clip.name, file);
        file.close();

        if (!ok) {
            // A half file is worse than none: SD.exists() would accept it later
            // and playWav() would then fail on it every boot.
            SD.remove(path);
            ++failed;
            continue;
        }

        ++downloaded;
    }

    Serial.printf("Clips in %s: %u already there, %u downloaded, %u pending.\n",
                  lang.c_str(), present, downloaded, failed);
}
