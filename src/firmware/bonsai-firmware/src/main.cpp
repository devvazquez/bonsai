#include <Arduino.h>
#include <Camera.h>
#include <SD.h>
#include <SPI.h>
#include <WiFiManager.h>
#include <Audio.h>
#include <ArduinoJson.h>
#include <Lang.h>

// The Arduino loop task gets 8 KB by default, and that is not enough for what
// runs nested inside look(): HTTPClient, mbedTLS and the audio sink all stack
// up in the same frame. It showed up as a stack canary panic in loopTask on the
// first /look after the streaming rewrite, not as anything that looked like
// memory at all. 16 KB leaves real headroom.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

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
constexpr const char* kLookFile          = "/look.wav";
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

// ---------------------------------------------------------------------------
// Serial configuration console.
//
// This is currently the only way to configure a board. The BLE service that
// used to call wifi.addNetwork() went with the audio rewrite and nothing
// replaced it, and there is no AP mode, so a freshly flashed board has no way
// to be told a network: it boots, finds no credentials and sits reconnecting
// for ever. Open the serial monitor at 115200 and type "help".
//
// Each command takes the rest of the line verbatim, so SSIDs and passwords with
// spaces, colons or quotes in them need no escaping.
// ---------------------------------------------------------------------------

String pendingSsid;

// Defined below, next to the button handling it shares its behaviour with.
void look();

// ---------------------------------------------------------------------------
// Background photo writer.
//
// Writing the JPEG is pure bookkeeping — nobody is waiting to look at it — so
// it has no business sitting between the button and the spoken answer. A task
// does it instead, and the card lock lets it slot into the seconds this board
// spends waiting for the backend, rather than merely being pushed to the end.
//
// The lock is not optional. During playback the amplifier owns GPIO8, which is
// the card's MISO (see Audio.h), so the card is unusable for the whole clip; a
// write landing there would fail and could leave the filesystem mid-update.
// ---------------------------------------------------------------------------

constexpr const char* kPhotoDir = "/photos";

struct PhotoJob {
    uint8_t* buf;
    size_t   len;
};

SemaphoreHandle_t sdLock     = nullptr;
QueueHandle_t     photoQueue = nullptr;

// Counted once, so numbering carries on across reboots instead of overwriting.
uint32_t countPhotos() {
    uint32_t n = 0;
    File dir = SD.open(kPhotoDir);
    if (dir) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) { ++n; f.close(); }
        dir.close();
    }
    return n;
}

void photoTask(void*) {
    uint32_t index   = 0;
    bool     counted = false;

    for (;;) {
        PhotoJob job;
        if (xQueueReceive(photoQueue, &job, portMAX_DELAY) != pdTRUE) continue;

        const uint32_t tQueued = millis();
        xSemaphoreTakeRecursive(sdLock, portMAX_DELAY);
        const uint32_t tStart = millis();

        if (!SD.exists(kPhotoDir)) SD.mkdir(kPhotoDir);
        if (!counted) { index = countPhotos(); counted = true; }

        char path[48];
        snprintf(path, sizeof(path), "%s/%05u.jpg", kPhotoDir, index);

        bool ok = false;
        File f = SD.open(path, FILE_WRITE);
        if (f) {
            ok = f.write(job.buf, job.len) == job.len;
            f.close();
        }
        xSemaphoreGiveRecursive(sdLock);

        if (ok) {
            ++index;
            Serial.printf("  [t] photo %s: %u KB, waited %u ms for the card, "
                          "wrote in %u ms (off the critical path)\n",
                          path, (unsigned)(job.len / 1024),
                          tStart - tQueued, millis() - tStart);
        } else {
            Serial.printf("photo: could not write %s\n", path);
        }
        heap_caps_free(job.buf);
    }
}

// Copies the frame and hands it over. The copy is needed because the camera has
// only kFrameBuffers buffers and wants this one back immediately.
void savePhotoInBackground(const uint8_t* jpeg, size_t len) {
    if (!photoQueue) return;

    uint8_t* copy = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!copy) {
        Serial.println("photo: no PSRAM for a copy, not saving this one");
        return;
    }
    memcpy(copy, jpeg, len);

    PhotoJob job{copy, len};
    if (xQueueSend(photoQueue, &job, 0) != pdTRUE) {
        Serial.println("photo: the writer is still busy, dropping this one");
        heap_caps_free(copy);
    }
}

void printConfig() {
    Serial.println(F("--- config ---"));
    Serial.printf("lang        : %s\n", configDoc["lang"] | "(unset)");
    const char* url = configDoc["backend_url"] | "";
    Serial.printf("backend_url : %s\n", url[0] ? url : "(unset)");

    String   ssids[WIFI_MAX_NETWORKS];
    String   passes[WIFI_MAX_NETWORKS];
    uint32_t count = 0;
    WiFiManager::loadNetworksFromJson(configDoc, ssids, passes, count);

    if (count == 0) {
        // ASCII only in console output: the serial terminal renders anything
        // else as question marks.
        Serial.println(F("networks    : none - the board cannot connect"));
    } else {
        for (uint32_t i = 0; i < count; ++i) {
            // The password is only ever shown as a length: this output gets
            // pasted into bug reports and chat windows.
            Serial.printf("network %u   : \"%s\" (password %u chars)\n",
                          i, ssids[i].c_str(), passes[i].length());
        }
    }
    Serial.printf("SD          : %s\n",
                  SD.cardType() == CARD_NONE ? "not mounted" : "mounted");
    Serial.println(F("--------------"));
}

void printHelp() {
    Serial.println(F(
        "\nCommands (the rest of the line is taken as-is, no quoting):\n"
        "  ssid <name>       remember an SSID, then give it a password\n"
        "  pass <secret>     set the password for the SSID above and save\n"
        "  wifi del <name>   forget a network\n"
        "  lang <code>       e.g. ca-ES, es-ES (clips re-download per language)\n"
        "  backend <url>     backend base URL, no trailing slash\n"
        "  look              take a photo, describe it out loud (as the button)\n"
        "  scan              list the 2.4 GHz networks in range\n"
        "  show              print the current config\n"
        "  save              write /config.json now\n"
        "  reboot            restart the board\n"));
}

void handleCommand(const String& raw) {
    String line = raw;
    line.trim();
    if (line.length() == 0) return;

    // Split on the first space: "verb" plus everything after it, untouched.
    const int   sep  = line.indexOf(' ');
    const String verb = sep < 0 ? line : line.substring(0, sep);
    const String rest = sep < 0 ? String("") : line.substring(sep + 1);

    if (verb == "help" || verb == "?") {
        printHelp();
    } else if (verb == "show") {
        printConfig();
    } else if (verb == "look") {
        // The same thing a single button press does, minus the button. Useful
        // when the board is on a bench and nobody is there to press it.
        look();
    } else if (verb == "scan") {
        // Worth having on the device rather than trusting a laptop's scan: this
        // radio is 2.4 GHz only, so what the PC can see is not the question.
        Serial.println(F("Scanning (2.4 GHz only - this radio has no 5 GHz)..."));
        const int n = WiFi.scanNetworks();
        if (n <= 0) {
            Serial.println(F("Nothing found. Any 5 GHz network is invisible here."));
        } else {
            for (int i = 0; i < n; ++i) {
                Serial.printf("  %2d) %-32s ch%-3d %4d dBm%s\n", i,
                              WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
                              WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "  (open)" : "");
            }
        }
        WiFi.scanDelete();
    } else if (verb == "ssid") {
        if (rest.length() == 0) { Serial.println(F("ssid: needs a name")); return; }
        pendingSsid = rest;
        Serial.printf("SSID \"%s\" held - now send: pass <secret>\n",
                      pendingSsid.c_str());
    } else if (verb == "pass") {
        if (pendingSsid.length() == 0) {
            Serial.println(F("pass: send \"ssid <name>\" first"));
            return;
        }
        // addNetwork() saves through the onChanged callback setConfig() was
        // given, so there is nothing to write here.
        wifi.addNetwork(pendingSsid, rest);
        Serial.printf("Saved \"%s\" with a %u character password.\n",
                      pendingSsid.c_str(), rest.length());
        pendingSsid = "";
        Serial.println(F("Send \"reboot\" to connect with it."));
    } else if (verb == "wifi" && rest.startsWith("del ")) {
        const String ssid = rest.substring(4);
        wifi.removeNetwork(ssid);
        Serial.printf("Forgot \"%s\".\n", ssid.c_str());
    } else if (verb == "lang") {
        if (rest.length() == 0) { Serial.println(F("lang: needs a code")); return; }
        bonsai::setLang(rest);
        Serial.printf("Language is now %s.\n", rest.c_str());
    } else if (verb == "backend") {
        configDoc["backend_url"] = rest;
        saveConfig();
        Serial.printf("Backend is now \"%s\".\n", rest.c_str());
    } else if (verb == "save") {
        saveConfig();
        Serial.println(F("Wrote /config.json."));
    } else if (verb == "reboot") {
        Serial.println(F("Rebooting..."));
        Serial.flush();
        delay(100);
        ESP.restart();
    } else {
        Serial.printf("Unknown command \"%s\" - try \"help\".\n", verb.c_str());
    }
}

// What a single button press does: photograph whatever is in front of the
// wearer, send it to /look, and say the answer out loud.
//
// The reply is written to the card rather than held in RAM. It is a whole
// spoken sentence, so it is the same few-hundred-KB that broke the clip
// downloads, and playWav() wants a file anyway.
void look() {
    if (!wifi.isConnected()) {
        Serial.println("look: no WiFi.");
        audio.playDefault(DefaultAudios::NO_WIFI);
        return;
    }
    if (String(configDoc["backend_url"] | "").length() == 0) {
        Serial.println("look: backend_url is not set - send \"backend <url>\".");
        audio.playDefault(DefaultAudios::MISSING_CONFIG);
        return;
    }

    const uint32_t tAll = millis();

    // The camera throws away kFrameBuffers frames before keeping one, so this
    // is several frame times, not one — and at UXGA a frame is not cheap.
    const uint32_t tCap = millis();
    camera_fb_t* fb = camera.capture();
    const uint32_t capMs = millis() - tCap;
    if (!fb) {
        Serial.println("look: the camera returned no frame.");
        return;
    }
    Serial.printf("  [t] capture: %u ms for %u KB\n",
                  capMs, (unsigned)(fb->len / 1024));

    // Queued, not written: the card write happens on the other task while the
    // backend is thinking.
    savePhotoInBackground(fb->buf, fb->len);

    // Straight from the socket to the amplifier. No file, no second pass over
    // the card, and the first word is spoken while the rest of the sentence is
    // still being generated — the card round trip used to add the whole
    // download plus a re-read before any sound came out at all.
    String   spoken;
    uint32_t firstSampleMs = 0;
    bool     started       = false;

    WiFiManager::AudioSink sink;
    sink.onStart = [&](uint32_t rate) {
        firstSampleMs = millis() - tAll;
        started       = true;
        Serial.printf("  [t] first sample at %u ms, %u Hz\n", firstSampleMs,
                      rate ? rate : 16000u);
        return audio.beginStream(rate);
    };
    sink.onChunk = [&](const uint8_t* pcm, size_t len) {
        audio.writeStream(pcm, len);
        return true;
    };

    const uint32_t tReq = millis();
    const bool ok = wifi.lookStreaming(fb->buf, fb->len, sink, &spoken);
    if (started) audio.endStream();   // owed unconditionally: it holds GPIO8
    const uint32_t reqMs = millis() - tReq;

    camera.release(fb);

    if (!ok) {
        Serial.println("look: nothing was spoken.");
        return;
    }

    // "to answer" is the number that matters: button pressed to first sound.
    Serial.printf("  [t] TOTAL %u ms = capture %u + speak %u   (to answer: %u ms)\n",
                  millis() - tAll, capMs, reqMs, firstSampleMs);
}

// Non-blocking: collects a line across loop() iterations and never waits on
// input, so the button and the WiFi keep being serviced while someone types.
void consoleLoop() {
    static String line;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            const String done = line;
            line = "";
            handleCommand(done);
            continue;
        }
        if (line.length() < 200) line += c;
    }
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
            // ASCII: the serial terminal shows anything else as question marks.
            Serial.printf("WiFi connected - http://%s\n", detail.c_str());
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
    // Recursive, because the paths that write the card nest: look() takes the
    // lock and the request inside it takes it again for the download.
    sdLock     = xSemaphoreCreateRecursiveMutex();
    photoQueue = xQueueCreate(2, sizeof(PhotoJob));

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
    wifi.setSdLock(sdLock);
    wifi.begin();

    // Core 0: the Arduino loop runs on core 1, so a card write never competes
    // with the button polling. Low priority — nothing waits on this.
    xTaskCreatePinnedToCore(photoTask, "photo", 4096, nullptr, 1, nullptr, 0);

    if (!camera.begin()) {
        Serial.println("Camera initialization failed.");
        while(1);
    }

    // The single biggest lever on how long an answer takes.
    //
    // The photo goes up base64'd, so its size is 4/3 of the JPEG on the wire.
    // UXGA gives ~55 KB, which is ~73 KB to upload; measured against the same
    // backend, a 320x240 frame answered in 1.8 s while the board sat at 4.8 s,
    // and on a phone hotspot the big upload also failed outright often enough
    // to matter ("send payload failed"). SVGA is ~15 KB and still far more
    // detail than describing a room needs. Override with "frame_size".
    const String wanted = configDoc["frame_size"] | "svga";
    framesize_t  size   = FRAMESIZE_SVGA;
    if      (wanted == "uxga") size = FRAMESIZE_UXGA;
    else if (wanted == "sxga") size = FRAMESIZE_SXGA;
    else if (wanted == "xga")  size = FRAMESIZE_XGA;
    else if (wanted == "svga") size = FRAMESIZE_SVGA;
    else if (wanted == "vga")  size = FRAMESIZE_VGA;
    else if (wanted == "qvga") size = FRAMESIZE_QVGA;
    camera.setFrameSize(size);
    Serial.printf("Camera frame size: %s\n", wanted.c_str());

    //Download default audios if they are missing.
    audio.downloadDefaultAudiosIfMissing(wifi);

    printConfig();
    Serial.println(F("Type \"help\" for the configuration console."));

    if(isFirstBoot) {
        audio.playDefault(DefaultAudios::FIRST_BOOT);
    }
}

void loop() {

    consoleLoop();

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
            look();
        } else if (pressCount >= 2) {
            // double press — add action here
            audio.playDefault(DefaultAudios::START_TALKING);
        }
        pressCount = 0;
    }


    wifi.loop();
}
