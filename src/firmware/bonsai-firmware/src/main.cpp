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

// Bus speeds to try, fastest first. The first one normally takes it; the slower
// entries only matter for a marginal card or a board that has been reworked.
//
// Worth knowing before touching this list: bus speed was never what broke this
// board. A card that mounts and then fails every transfer looks exactly like a
// clock that is too fast, and it is not — mounting happens at the 400 kHz the
// spec mandates for identification, so a card can report its type and size
// correctly while nothing else works. Chasing that costs a lot of flash cycles.
// See the note on kAmpEnable in Audio.h for what the real cause was.
constexpr uint32_t kSdFrequencies[] = {4000000, 1000000, 400000};

constexpr const char* kSdProbeFile = "/.bonsai-probe";

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

// Mounts the card at the fastest speed it will actually work at.
//
// A successful mount is not enough to go on: it only proves the card answered
// during the 400 kHz init, not that it will answer at the bus speed that
// follows. So every candidate has to write a byte and read it back before it is
// accepted, which is the part that was failing before.
bool mountSd() {
    bool mounted = false;

    for (const uint32_t hz : kSdFrequencies) {
        // Only ever unmount something that mounted: SD.end() on a card that
        // never came up leaves the driver in a state the next begin() will not
        // recover from, which costs every remaining candidate.
        if (mounted) {
            SD.end();
            mounted = false;
        }

        if (!SD.begin(kSdCs, SPI, hz)) {
            Serial.printf("SD: no mount at %u kHz.\n", hz / 1000);
            continue;
        }
        mounted = true;

        bool ok = false;
        File probe = SD.open(kSdProbeFile, FILE_WRITE);
        if (probe) {
            const uint8_t written = probe.write('B');
            probe.close();

            // Read it back: a write that reports success but lands nowhere is
            // the failure mode this whole exercise is about.
            File verify = SD.open(kSdProbeFile, FILE_READ);
            if (written == 1 && verify) {
                ok = verify.read() == 'B';
                verify.close();
            }
            SD.remove(kSdProbeFile);
        }

        if (!ok) {
            Serial.printf("SD: mounted at %u kHz but cannot write; slowing down.\n",
                          hz / 1000);
            continue;
        }

        const uint8_t type = SD.cardType();
        const char*   name = type == CARD_NONE ? "none" :
                             type == CARD_MMC  ? "MMC"  :
                             type == CARD_SD   ? "SDSC" :
                             type == CARD_SDHC ? "SDHC" : "unknown";
        Serial.printf("SD: %s, %llu MB, read/write at %u kHz.\n",
                      name, SD.cardSize() >> 20, hz / 1000);
        return true;
    }

    if (mounted) SD.end();
    return false;
}

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

    if (!mountSd()) {
        Serial.println("SD mount failed: no config and no clips, treating this as a first boot.");
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
