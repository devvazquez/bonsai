#include <Arduino.h>
#include <Camera.h>
#include <SD.h>
#include <SPI.h>
#include <WiFiManager.h>
#include <Audio.h>
#include <ArduinoJson.h>
#include <Lang.h>

// The tactile switch (SW1). The first board wired it to U1 pad 11 — GPIO9,
// which is D10 = MOSI of the microSD bus — so holding the button shorted MOSI
// to ground and a pull-up on it fought the card. That trace is cut and the
// switch leg goes to U1 pad 4 = GPIO4 = D3 instead, the nearest free pad.
// See hardware/REWORK.md.
#define BUTTON_PIN 4

// microSD slot on the Sense expansion board: the default SPI pins (SCK 7,
// MISO 8, MOSI 9) with CS on GPIO21. Not the variant's default SS, which is
// GPIO44 — the RX pad — and is what a bare SD.begin() would drive instead,
// so the card would never mount.
constexpr int kSdCs = 21;

/* TODO: 
    - Implement backend API.
    - Download TTS defaults. X
    - Groq tools:
        - Stop searching for wifi.
        - Change language.
    -  Config:
        - wifi networks (ssid, password) X
*/


Camera      camera;
WiFiManager wifi;
Audio       audio;

namespace {

constexpr const char* kConfigFile        = "/config.json";
constexpr const char* kDefaultLang       = "ca-ES";
constexpr const char* kDefaultBackendUrl = "";

DynamicJsonDocument configDoc(4096);
bool                isFirstBoot = false;

void applyConfigDefaults() {
    if (!configDoc.containsKey("lang"))        configDoc["lang"]        = kDefaultLang;
    if (!configDoc.containsKey("backend_url")) configDoc["backend_url"] = kDefaultBackendUrl;
}

// Load /config.json from the SD card.
// Sets isFirstBoot when the file is missing or unreadable, so callers can run first-time setup.
void loadConfig() {
    if (!SD.exists(kConfigFile)) {
        isFirstBoot = true;
        applyConfigDefaults();
        return;
    }

    File file = SD.open(kConfigFile, FILE_READ);
    if (!file) {
        isFirstBoot = true;
        applyConfigDefaults();
        return;
    }

    String content = file.readString();
    file.close();

    if (deserializeJson(configDoc, content) != DeserializationError::Ok) {
        isFirstBoot = true;
        configDoc.clear();
    }

    applyConfigDefaults();
}

void saveConfig() {
    File file = SD.open(kConfigFile, FILE_WRITE);
    if (!file) {
        // Worth saying out loud: silently dropping this is what made a board
        // that could not reach the card look like one stuck on its first boot,
        // asking for the WiFi again every time.
        Serial.printf("Could not open %s for writing: config not saved.\n",
                      kConfigFile);
        return;
    }
    serializeJson(configDoc, file);
    file.close();
}

}  // namespace


// Language (declared in lib/Lang/Lang.h). It lives here because this is where
// the config and saveConfig() are, so there is only ever one copy of it.
namespace bonsai {

String lang() {
    String l = configDoc["lang"] | kDefaultLang;
    // The config says "ca-ES"; the backend wants "ca".
    const int guio = l.indexOf('-');
    if (guio > 0) l = l.substring(0, guio);
    l.toLowerCase();
    return l;
}

void setLang(const String& nou) {
    if (nou.length() == 0) return;
    if (String(configDoc["lang"] | "") == nou) return;   // nothing to save
    configDoc["lang"] = nou;
    saveConfig();
    Serial.printf("Idioma: %s\n", lang().c_str());
}

}  // namespace bonsai


void onWiFiStatus(WiFiStatus status, const String& detail) {
    switch (status) {
        case WiFiStatus::Connected:
            Serial.printf("WiFi connected — http://%s\n", detail.c_str());
            break;
        case WiFiStatus::Disconnected:
            Serial.println("Wifi Disconnected");
            audio.playDefault(DefaultAudios::NO_WIFI);
        break;
        case WiFiStatus::Reconnecting:
            Serial.println("Wifi Reconnecting");
            break;
    }
}

void setup() {
    //Set the pullup config fo the button pin.
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    
    Serial.begin(115200);

    // The card holds /config.json and the default clips, so a failed mount is
    // worth saying out loud: without it loadConfig() finds nothing and every
    // boot looks like the first one.
    SPI.begin(SCK, MISO, MOSI, kSdCs);
    if (!SD.begin(kSdCs)) {
        Serial.println("SD mount failed: no config and no clips, treating this as a first boot.");
    } else {
        // A mount that succeeds and then fails every read looks identical to a
        // missing card from the log alone, so say what actually answered.
        const uint8_t type = SD.cardType();
        const char*   name = type == CARD_NONE ? "none" :
                             type == CARD_MMC  ? "MMC"  :
                             type == CARD_SD   ? "SDSC" :
                             type == CARD_SDHC ? "SDHC" : "unknown";
        Serial.printf("SD mounted: %s, %llu MB\n", name, SD.cardSize() >> 20);
    }

    if (!audio.begin()) {
        Serial.println("I2S init failed: there will be no sound.");
    }

    loadConfig();
    if (isFirstBoot) {
        Serial.println("Detected first boot, writing config defaults.");
        saveConfig();
    }
    wifi.onStatusChange(onWiFiStatus);
    wifi.setConfig(configDoc, saveConfig);
    wifi.begin();

    if (!camera.begin()) {
        Serial.println("Camera initialization failed.");
        while(1);
    }

    //Download default audios if they are missing.
    audio.downloadDefaultAudiosIfMissing(wifi);

    if(isFirstBoot) {
        audio.playDefault(DefaultAudios::FIRST_BOOT);
    }
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
            Serial.printf("BUTTON pressed on GPIO%d\n", BUTTON_PIN);
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
