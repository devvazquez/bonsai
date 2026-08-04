#include <Arduino.h>
#include <Camera.h>
#include <SD.h>
#include <WiFiManager.h>
#include <Audio.h>

#define BUTTON_PIN 01

Camera      camera;
WiFiManager wifi;
Audio audio;

void onWiFiStatus(WiFiStatus status, const String& detail) {
    switch (status) {
        case WiFiStatus::Connected:
            Serial.printf("WiFi connected — http://%s\n", detail.c_str());
            break;
        case WiFiStatus::Disconnected:
            Serial.println("Wifi Disconnected");
        break;
        case WiFiStatus::Reconnecting:
            Serial.println("Wifi Reconnecting");
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

    wifi.onStatusChange(onWiFiStatus);
    wifi.begin();

    // tts.begin(wifi);
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
            audio.playDefault(DefaultAudios::START_TALKING);
        }
        pressCount = 0;
    }


    wifi.loop();
}
