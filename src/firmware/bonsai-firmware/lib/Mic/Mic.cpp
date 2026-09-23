#include "Mic.h"
#include <esp_heap_caps.h>
#include <math.h>

bool Mic::begin(uint32_t gain) {
    if (_rx) return true;
    _gain      = gain < 1 ? 1 : (gain > 32 ? 32 : gain);
    _dcPrevIn  = 0;
    _dcPrevOut = 0;

    // I2S_NUM_0 and not 1: on the ESP32-S3 the PDM receiver only exists on the
    // first controller. The amplifier's TX channel is on that controller too,
    // but they are two simplex channels with their own clocks, and this one is
    // only ever open while nothing is playing.
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // Four descriptors of one frame each: a fifth of a second of slack, so a
    // caller that is busy writing to the socket does not lose samples.
    chan_cfg.dma_desc_num  = 6;
    chan_cfg.dma_frame_num = kMicFrame;

    esp_err_t err = i2s_new_channel(&chan_cfg, nullptr, &_rx);
    if (err != ESP_OK) {
        Serial.printf("mic: i2s_new_channel failed: %s\n", esp_err_to_name(err));
        _rx = nullptr;
        return false;
    }

    i2s_pdm_rx_config_t cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(kMicRate),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                   I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = (gpio_num_t)kMicClk,
            .din = (gpio_num_t)kMicDin,
            .invert_flags = { .clk_inv = false },
        },
    };
    err = i2s_channel_init_pdm_rx_mode(_rx, &cfg);
    if (err != ESP_OK) {
        Serial.printf("mic: i2s_channel_init_pdm_rx_mode failed: %s\n",
                      esp_err_to_name(err));
        i2s_del_channel(_rx);
        _rx = nullptr;
        return false;
    }

    err = i2s_channel_enable(_rx);
    if (err != ESP_OK) {
        Serial.printf("mic: i2s_channel_enable failed: %s\n", esp_err_to_name(err));
        i2s_del_channel(_rx);
        _rx = nullptr;
        return false;
    }
    return true;
}

void Mic::end() {
    if (!_rx) return;
    i2s_channel_disable(_rx);
    i2s_del_channel(_rx);
    _rx = nullptr;
}

size_t Mic::read(int16_t* out, size_t samples, uint32_t timeoutMs) {
    if (!_rx || !out || samples == 0) return 0;
    size_t got = 0;
    const esp_err_t err = i2s_channel_read(_rx, out, samples * sizeof(int16_t),
                                           &got, pdMS_TO_TICKS(timeoutMs));
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        Serial.printf("mic: read failed: %s\n", esp_err_to_name(err));
        return 0;
    }

    // DC out, gain in, one pass. R = 2036/2048 at 16 kHz puts the corner around
    // 12 Hz: well under anything a voice does, and it takes an offset that would
    // otherwise swamp the RMS out in milliseconds.
    const size_t n = got / sizeof(int16_t);
    for (size_t i = 0; i < n; ++i) {
        const int32_t x = out[i];
        int32_t y = x - _dcPrevIn + ((_dcPrevOut * 2036) >> 11);
        _dcPrevIn  = x;
        _dcPrevOut = y;

        y *= (int32_t)_gain;
        if (y >  32767) y =  32767;
        if (y < -32768) y = -32768;
        out[i] = (int16_t)y;
    }
    return n;
}

// ---------------------------------------------------------------------------

bool VoiceGate::begin() { return begin(Config{}); }

bool VoiceGate::begin(const Config& cfg) {
    _cfg     = cfg;
    _frameMs = (uint32_t)(kMicFrame * 1000 / kMicRate);

    const size_t cap = (size_t)kMicRate * _cfg.prerollMs / 1000;
    if (!_ring || _ringCap < cap) {
        if (_ring) heap_caps_free(_ring);
        if (_prerollOut) heap_caps_free(_prerollOut);
        _ring = (int16_t*)heap_caps_malloc(cap * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        _prerollOut = (int16_t*)heap_caps_malloc(cap * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        if (!_ring || !_prerollOut) {
            Serial.println("mic: no PSRAM for the pre-roll buffer");
            end();
            return false;
        }
        _ringCap = cap;
    }
    reset();
    return true;
}

void VoiceGate::end() {
    if (_ring)       heap_caps_free(_ring);
    if (_prerollOut) heap_caps_free(_prerollOut);
    _ring = _prerollOut = nullptr;
    _ringCap = 0;
    reset();
}

void VoiceGate::reset() {
    _prevSample  = 0;
    _noise       = 0.0f;
    _frames      = 0;
    _calibrated  = false;
    _blockMin    = 0xFFFF;
    _blockFrames = 0;
    _minsIdx     = 0;
    _minsFilled  = 0;
    _speaking    = false;
    _voiceScore  = 0;
    _quietMs     = 0;
    _speechMs    = 0;
    _startedMs   = 0;
    _peak        = 0;
    _speechLvl   = 0.0f;
    _sentPreroll = false;
    _ringLen     = 0;
    _ringHead    = 0;
    _prerollOutSamples = 0;
}

// RMS of the pre-emphasised frame. 0.9375 = 15/16 keeps it in integers; the
// carry makes the filter continuous across frames.
uint16_t VoiceGate::level(const int16_t* frame, size_t samples) {
    if (samples == 0) return 0;
    uint64_t sum  = 0;
    int32_t  prev = _prevSample;
    for (size_t i = 0; i < samples; ++i) {
        const int32_t x = frame[i];
        const int32_t d = x - ((prev * 15) >> 4);
        sum += (uint64_t)((int64_t)d * d);
        prev = x;
    }
    _prevSample = prev;
    const double r = sqrt((double)sum / (double)samples);
    return (uint16_t)(r > 65535.0 ? 65535.0 : r);
}

void VoiceGate::trackFloor(uint16_t lvl) {
    if (lvl < _blockMin) _blockMin = lvl;
    if (++_blockFrames >= kFramesPerBlock) {
        _mins[_minsIdx] = _blockMin;
        _minsIdx = (_minsIdx + 1) % kMinBlocks;
        if (_minsFilled < kMinBlocks) ++_minsFilled;
        _blockMin    = 0xFFFF;
        _blockFrames = 0;
    }

    uint16_t cand = _blockMin;
    for (int i = 0; i < _minsFilled; ++i) if (_mins[i] < cand) cand = _mins[i];
    if (cand == 0xFFFF) return;

    // Down fast: a quieter room is believed at once. Up slowly, and slower
    // still while someone is talking, so that one long unbroken sentence
    // cannot lift the bar out from under its own end.
    if (cand < _noise) _noise = _noise * 0.7f + cand * 0.3f;
    else               _noise += (cand - _noise) * (_speaking ? 0.01f : 0.05f);
}

void VoiceGate::pushPreroll(const int16_t* frame, size_t samples) {
    if (!_ring || _ringCap == 0) return;
    for (size_t i = 0; i < samples; ++i) {
        _ring[_ringHead] = frame[i];
        _ringHead = (_ringHead + 1) % _ringCap;
        if (_ringLen < _ringCap) ++_ringLen;
    }
}

void VoiceGate::backToWaiting() {
    _speaking    = false;
    _voiceScore  = 0;
    _quietMs     = 0;
    _speechMs    = 0;
    _speechLvl   = 0.0f;
    _sentPreroll = false;
    _ringLen     = 0;
    _ringHead    = 0;
}

VoiceGate::Verdict VoiceGate::feed(const int16_t* frame, size_t samples,
                                   uint32_t nowMs) {
    if (_startedMs == 0) _startedMs = nowMs;
    _prerollOutSamples = 0;
    ++_frames;

    // The first frames are thrown away: the DMA hands back whatever was in the
    // buffers before the mic and the DC blocker settled.
    if (_frames <= 3) {
        level(frame, samples);          // primes the pre-emphasis carry
        pushPreroll(frame, samples);
        return Verdict::Listening;
    }

    const uint16_t lvl = level(frame, samples);
    if (lvl > _peak) _peak = lvl;

    if (!_calibrated) {
        // The quietest frame so far, not the average: someone already talking
        // pulls an average up, but their pauses still pull a minimum down.
        _noise = (_noise == 0.0f || lvl < _noise) ? lvl : _noise;
        if ((_frames - 3) * _frameMs >= _cfg.calibrateMs) {
            _calibrated = true;
            Serial.printf("  [t] mic: noise floor %u\n", (unsigned)_noise);
        }
    }
    trackFloor(lvl);

    const uint16_t startThr = max((uint16_t)(_noise * _cfg.startFactor), _cfg.startFloor);
    uint16_t keepThr = max((uint16_t)(_noise * _cfg.keepFactor), _cfg.keepFloor);
    keepThr = max(keepThr, (uint16_t)(_speechLvl * _cfg.endRelative));

    if (_cfg.debug && _frames % 5 == 0) {
        Serial.printf("  vad lvl %5u floor %5u start %5u keep %5u %s\n", lvl,
                      (unsigned)_noise, startThr, keepThr,
                      _speaking ? "SPEECH" : "-");
    }

    if (!_speaking) {
        // Voice may start during the calibration too: only the floor waits
        // for it, the detection does not.
        if (lvl >= startThr) ++_voiceScore;
        else if (_voiceScore > 0) --_voiceScore;

        if (_voiceScore < 3) {
            pushPreroll(frame, samples);
            if (nowMs - _startedMs >= _cfg.startTimeoutMs) return Verdict::Silence;
            return Verdict::Listening;
        }

        _speaking  = true;
        _quietMs   = 0;
        _speechMs  = 3 * _frameMs;
        _speechLvl = lvl;

        // Just latched. Hand back what was in the ring: the syllable that gave
        // the voice away is in there, plus whatever came before it.
        if (!_sentPreroll && _ringLen > 0) {
            size_t idx = (_ringHead + _ringCap - _ringLen) % _ringCap;
            for (size_t i = 0; i < _ringLen; ++i) {
                _prerollOut[i] = _ring[idx];
                idx = (idx + 1) % _ringCap;
            }
            _prerollOutSamples = _ringLen;
        }
        _sentPreroll = true;
        return Verdict::Voice;
    }

    _speechMs += _frameMs;

    if (lvl >= keepThr) {
        _quietMs = 0;
        // Envelope of the voice: jumps to a louder frame, fades by half over
        // ~7 s, so the end bar follows the speaker without chasing each word.
        _speechLvl = lvl > _speechLvl ? lvl : _speechLvl * 0.998f;
    } else {
        _quietMs += _frameMs;
        if (_quietMs >= _cfg.hangoverMs) {
            // Enough quiet to call it finished. The hangover itself is part of
            // the recording and that is intentional: Whisper does better with a
            // little silence at the end than with a sentence cut at the last
            // consonant.
            const uint32_t spoken = _speechMs > _quietMs ? _speechMs - _quietMs : 0;
            if (spoken < _cfg.minSpeechMs) {
                // A cough, a door. Go back to waiting rather than sending it.
                backToWaiting();
                return Verdict::Listening;
            }
            return Verdict::Done;
        }
    }

    if (_speechMs >= _cfg.maxSpeechMs) return Verdict::Done;
    return Verdict::Voice;
}
