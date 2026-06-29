#pragma once

#include <Arduino.h>

// MAX98357A wiring
#define I2S_DIN_PIN  9
#define I2S_BCLK_PIN 8
#define I2S_LRC_PIN  7
#define I2S_SD_PIN   6   // amp shutdown — HIGH = enabled

#define TTS_VOICE_ID    "JBFqnCBsd6RMkjVDRZzb"
#define TTS_SAMPLE_RATE 16000

class WiFiManager;

class TTS {
public:
    void begin(WiFiManager& wifi);
    void speak(const String& text);

private:
    WiFiManager* _wifi = nullptr;
    static constexpr const char* _apiKey = TTS_API_KEY;
};