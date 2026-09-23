#pragma once
#include <Arduino.h>
#include <driver/i2s_pdm.h>

// The PDM microphone on the XIAO ESP32-S3 Sense expansion board (MSM261D3526H1,
// on the same board as the camera connector and the microSD slot).
//
// These two pins are the Sense board's own wiring and do not appear on the
// schematic in hardware/: nothing on the PCB touches them, so there is nothing
// to conflict with. CLK is an output the ESP32 drives; DATA is the one-bit
// oversampled stream coming back.
constexpr int kMicClk = 42;
constexpr int kMicDin = 41;

// Everything here talks in 16 kHz mono PCM16, which is what /ask wants
// (`micRate`) and what Whisper transcribes without resampling.
constexpr uint32_t kMicRate = 16000;

// 20 ms of audio. Short enough that the end of a sentence is noticed promptly,
// long enough that one frame's RMS means something.
constexpr size_t kMicFrame = kMicRate / 50;   // 320 samples

class Mic {
public:
    // Opens the PDM receiver. The sample rate is fixed; `gain` multiplies the
    // samples on the way out, with saturation.
    //
    // The gain is in software because it has to be: the ESP32-S3's PDM receiver
    // has no hardware amplifier and no high-pass filter (the `amplify_num` and
    // `hp_en` fields in the driver only exist on the chips that do), so both the
    // DC removal and the level are done here, per sample, on the way out of
    // read().
    //
    // The I2S TX channel that drives the amplifier lives on the same port, so
    // this is deliberately opened per recording and closed right after: nothing
    // records and plays at the same time on this board anyway, since the
    // amplifier owns the card's MISO while it is on.
    bool begin(uint32_t gain = 4);
    void end();
    bool isOpen() const { return _rx != nullptr; }

    // Fills one frame. Blocks up to `timeoutMs`; returns the samples actually
    // read, which is 0 on a timeout.
    size_t read(int16_t* out, size_t samples, uint32_t timeoutMs = 200);

private:
    i2s_chan_handle_t _rx   = nullptr;
    uint32_t          _gain = 4;

    // One-pole DC blocker, carried between frames. A PDM microphone sits on a
    // large offset, and an offset is indistinguishable from loudness once you
    // measure RMS to decide whether somebody is talking - so it has to go before
    // the gain, not after, or it is what gets amplified.
    int32_t _dcPrevIn  = 0;
    int32_t _dcPrevOut = 0;
};

// Decides when the wearer has finished talking.
//
// The problem it solves: the button is a tap, not a walkie-talkie, so nothing
// tells the device when the question is over. Fixed-length recordings are the
// obvious answer and a bad one — they either cut people off or make everyone
// wait for the slowest possible speaker.
//
// How it decides:
//
//   1. Loudness is measured on a pre-emphasised signal (x[n] - 0.94 x[n-1]),
//      not the raw one. Speech lives above ~300 Hz; fans, traffic, handling
//      noise and the PDM mic's own rumble live below it, and on the raw RMS
//      they are what sets the bar.
//   2. The noise floor is tracked with minimum statistics: the quietest 200 ms
//      block out of the last ~1.6 s. The first version averaged the first
//      250 ms after the mic opened and took that as the room — and whoever
//      started talking right after the clip had their own voice measured as
//      the room, with the start bar at 3.5x their voice. That was most of the
//      "I spoke and it heard nothing". A minimum is not fooled by speech,
//      because speech has gaps; it is only fooled by sound that never stops,
//      which is by definition the room.
//   3. Speech has to clear the floor by a margin for most of ~60 ms before it
//      counts as started, so a door or a click does not.
//   4. Once started, it ends after `hangoverMs` below the keep bar. That bar is
//      relative to the floor *and* to how loud the speaker actually was: a
//      floor underestimated in a noisy room otherwise leaves the room itself
//      above the bar, and the recording runs to the 15 s ceiling — which was
//      the other half of the failures.
//   5. Everything is bounded: no speech at all ends it, and so does talking for
//      too long.
//
// The frames from just before speech was detected are kept in a pre-roll ring
// and sent too. Without it the recording starts a syllable late and Whisper
// loses the first word, which is usually the one that matters.
class VoiceGate {
public:
    struct Config {
        // Silence needed to call the question finished. 800 ms is past the
        // pause someone leaves mid-sentence while thinking, and short enough
        // that the answer does not feel held back.
        uint32_t hangoverMs    = 800;
        // Nothing said at all: the button was pressed by accident, or the
        // person changed their mind.
        uint32_t startTimeoutMs = 7000;
        // Hard ceiling. The backend caps at 30 s; this is the device saying
        // enough well before that, because Whisper bills per second.
        uint32_t maxSpeechMs   = 15000;
        // Anything shorter than this was a noise, not a question.
        uint32_t minSpeechMs   = 300;
        // First estimate of the floor, before the minimum tracker has history.
        uint32_t calibrateMs   = 200;
        // How far above the floor a frame has to be to count as voice, and how
        // far it has to fall back to count as silence.
        float    startFactor   = 3.0f;
        float    keepFactor    = 1.8f;
        // Silence is also anything this far under the speaker's own level
        // (0.1 = -20 dB), whatever the floor thinks.
        float    endRelative   = 0.1f;
        // Absolute floors for both, for a room so quiet that a small multiple
        // of the noise is still nothing.
        uint16_t startFloor    = 150;
        uint16_t keepFloor     = 90;
        // How much audio to keep from before speech was detected.
        uint32_t prerollMs     = 300;
        // Prints level/floor/thresholds every 100 ms. "mic_debug": true in the
        // config; it is what to look at when tuning the numbers above.
        bool     debug         = false;
    };

    enum class Verdict {
        Listening,   // nothing to send yet
        Voice,       // this frame is part of the question
        Done,        // the question is over, and it was long enough to send
        Silence,     // gave up: nothing was ever said
    };

    bool begin();
    bool begin(const Config& cfg);
    void end();
    void reset();

    // One frame in, one decision out. `nowMs` is millis() at the caller.
    Verdict feed(const int16_t* frame, size_t samples, uint32_t nowMs);

    // Valid on the first Voice verdict only: the audio from before speech was
    // detected, oldest first, which must be sent ahead of the current frame.
    const int16_t* preroll() const { return _prerollOut; }
    size_t         prerollSamples() const { return _prerollOutSamples; }

    // For the log line: how loud the room was and how loud the speech was.
    uint16_t noiseFloor() const { return (uint16_t)_noise; }
    uint16_t peak() const { return _peak; }
    uint32_t speechMs() const { return _speechMs; }

private:
    uint16_t level(const int16_t* frame, size_t samples);
    void     trackFloor(uint16_t level);
    void     pushPreroll(const int16_t* frame, size_t samples);
    void     backToWaiting();

    static constexpr int kMinBlocks      = 8;    // x 200 ms = the floor's memory
    static constexpr int kFramesPerBlock = 10;

    Config   _cfg;
    uint32_t _frameMs   = 20;

    int32_t  _prevSample = 0;     // pre-emphasis carry
    float    _noise      = 0.0f;
    uint32_t _frames     = 0;
    bool     _calibrated = false;

    // Minimum statistics: the running minimum of the current block, and the
    // minima of the last kMinBlocks complete ones.
    uint16_t _blockMin    = 0xFFFF;
    int      _blockFrames = 0;
    uint16_t _mins[kMinBlocks] = {};
    int      _minsIdx     = 0;
    int      _minsFilled  = 0;

    bool     _speaking   = false;
    int      _voiceScore = 0;     // loud frames, minus quiet ones, before latching
    uint32_t _quietMs    = 0;     // trailing silence once speaking
    uint32_t _speechMs   = 0;
    uint32_t _startedMs  = 0;     // millis() of the first frame fed
    uint16_t _peak       = 0;
    float    _speechLvl  = 0.0f;  // slow-decay envelope of the voiced frames
    bool     _sentPreroll = false;

    // The pre-roll ring, in PSRAM: 300 ms at 16 kHz is 9.6 KB and the internal
    // heap is what WiFi and TLS live in.
    int16_t* _ring       = nullptr;
    size_t   _ringCap    = 0;     // in samples
    size_t   _ringLen    = 0;
    size_t   _ringHead   = 0;
    int16_t* _prerollOut = nullptr;
    size_t   _prerollOutSamples = 0;
};
