#include "TTS.h"
#include "../WiFi/WiFiManager.h"
#include <driver/i2s.h>

#define I2S_PORT I2S_NUM_0

void TTS::begin(WiFiManager& wifi) {
    _wifi = &wifi;

    pinMode(I2S_SD_PIN, OUTPUT);
    digitalWrite(I2S_SD_PIN, LOW);  // amp off until speaking

    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = TTS_SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 64,
        .use_apll             = false,
        .tx_desc_auto_clear   = true,
    };
    i2s_pin_config_t pins = {
        .bck_io_num   = I2S_BCLK_PIN,
        .ws_io_num    = I2S_LRC_PIN,
        .data_out_num = I2S_DIN_PIN,
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };
    i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    i2s_set_pin(I2S_PORT, &pins);
    i2s_zero_dma_buffer(I2S_PORT);
}

void TTS::speak(const String& text) {
    if (!_wifi || !_wifi->isConnected()) return;

    digitalWrite(I2S_SD_PIN, HIGH);  // enable amp

    _wifi->streamTTS(text, _apiKey, TTS_VOICE_ID, [](const uint8_t* buf, size_t n) {
        size_t written;
        i2s_write(I2S_PORT, buf, n, &written, portMAX_DELAY);
    });

    i2s_zero_dma_buffer(I2S_PORT);
    digitalWrite(I2S_SD_PIN, LOW);  // disable amp
}
