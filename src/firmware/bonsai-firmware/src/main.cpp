#include <Arduino.h>
#include <Camera.h>
#include <SD.h>
#include <BLEManager.h>
#include <WiFiManager.h>
#include <TTS.h>

#define BUTTON_PIN 01

Camera      camera;
BLEManager  ble;
WiFiManager wifi;
TTS         tts;

void onConfigReceived(const BonsaiConfig& cfg) {
    // OV3660 safe frameSize values: 8=VGA(640x480), 10=XGA(1024x768), 13=UXGA(1600x1200), 17=QXGA(2048x1536)
    uint32_t safe = min(cfg.frameSize, (uint32_t)FRAMESIZE_QXGA);
    camera.setFrameSize(static_cast<framesize_t>(safe));
}

void onWiFiCommand(const WiFiCommand& cmd) {
    if (cmd.action == WiFiCommand::Action::Add) {
        wifi.addNetwork(cmd.ssid, cmd.password);
    } else {
        wifi.removeNetwork(cmd.ssid);
    }
}

void onWiFiStatus(WiFiStatus status, const String& detail) {
    switch (status) {
        case WiFiStatus::Connected:
            ble.notify(("wifi:connected:" + detail).c_str());
            Serial.printf("WiFi connected — http://%s\n", detail.c_str());
            break;
        case WiFiStatus::Disconnected:
            ble.notify("wifi:disconnected");
            break;
        case WiFiStatus::Reconnecting:
            ble.notify("wifi:reconnecting");
            break;
        case WiFiStatus::APStarted:
            ble.notify(("wifi:ap:" + detail).c_str());
            Serial.printf("AP started — http://%s\n", detail.c_str());
            break;
    }
}

void setup() {
    Serial.begin(115200);
    SD.begin(); // Initialize SD card

    //Set the pullup config fo the button pin.
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    if (!camera.begin()) {
        Serial.println("Camera initialization failed");
        while(1);
    }

    ble.onConfigReceived(onConfigReceived);
    ble.onWiFiCommand(onWiFiCommand);
    ble.begin();

    wifi.onStatusChange(onWiFiStatus);
    wifi.begin();

    tts.begin(wifi);
}

void loop() {

    // Button debounce + single/double press detection.
    static bool     lastState       = HIGH;
    static uint32_t lastChange      = 0;
    static uint32_t lastPressTime   = 0;
    static uint8_t  pressCount      = 0;
    static bool     waitingDouble   = false;

    const uint32_t DEBOUNCE_MS      = 50;
    const uint32_t DOUBLE_PRESS_MS  = 250;

    uint32_t now = millis();
    bool currentState = digitalRead(BUTTON_PIN);

    if (currentState != lastState && now - lastChange > DEBOUNCE_MS) {
        lastChange = now;
        lastState  = currentState;
        if (currentState == LOW) {  // falling edge = press
            pressCount++;
            lastPressTime = now;
            waitingDouble = true;
        }
    }

    if (waitingDouble && now - lastPressTime > DOUBLE_PRESS_MS) {
        waitingDouble = false;
        if (pressCount == 1) {
            camera.save(SD, "/capture.jpg");
        } else if (pressCount >= 2) {
            // double press — add action here
        }
        pressCount = 0;
    }


    wifi.loop();
}
