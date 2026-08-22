#include <Arduino.h>
#include <Camera.h>
#include <SD.h>
#include <SPI.h>
#include <WiFiManager.h>
#include <Audio.h>
#include <ArduinoJson.h>
#include <Lang.h>
#include <SetupPortal.h>
#include <esp_timer.h>

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
SetupPortal portal;

namespace {

// Raised when the board has no network: either WiFiManager gave up on its
// retries, or something asked for the network and found nothing there. Only ever
// a flag. It is set from inside a request — possibly with the audio sink
// mid-stream and the card lock held — so the clip and the portal are run from
// loop(), and from one place, which is what stops the clip playing twice.
volatile bool offlineRequested = false;

constexpr const char* kConfigFile        = "/config.json";
constexpr const char* kLookFile          = "/look.wav";
constexpr const char* kDefaultLang       = "ca-ES";
constexpr const char* kDefaultBackendUrl = "";

// 8 KB and not 4: the setup portal adds the token and up to five
// ssid/password pairs on top of everything else that lives in here.
DynamicJsonDocument configDoc(8192);
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

// What a single button press does: photograph whatever is in front of the
// wearer, send it to /look, and say the answer out loud.
//
// The reply is written to the card rather than held in RAM. It is a whole
// spoken sentence, so it is the same few-hundred-KB that broke the clip
// downloads, and playWav() wants a file anyway.
void look() {
    if (!wifi.isConnected()) {
        // No clip and no portal from here: this runs inside loop()'s call, and
        // doing both from the same place the request came from is what played
        // the clip twice. Raise the flag and let loop() handle it once, the same
        // way it handles the retries running out.
        Serial.println("look: no WiFi.");
        offlineRequested = true;
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

// ---------------------------------------------------------------------------
// Setup access point.
//
// The board has no screen and no console, so this is the only way to tell it a
// network. It comes up in two situations, with a different page each time:
//
//   first boot        no credentials on the card, nothing to connect to, so
//                     setup() raises the AP before the WiFi stack is started
//                     and serves the onboarding walkthrough.
//   long press        a board that is already configured and running. The panel
//                     is served instead: everything on one screen, prefilled,
//                     for changing a password or a backend URL.
//
// Either way the portal ends by rebooting, so these never return once the user
// presses save.
// ---------------------------------------------------------------------------

bool needsSetup() {
    if (isFirstBoot) return true;

    String   ssids[WIFI_MAX_NETWORKS];
    String   passes[WIFI_MAX_NETWORKS];
    uint32_t count = 0;
    WiFiManager::loadNetworksFromJson(configDoc, ssids, passes, count);
    return count == 0;
}

// Writes the card through the same lock the photo task uses: on the long-press
// path that task is already running, and a save landing in the middle of a JPEG
// write is exactly what the lock exists to prevent. On the first-boot path the
// lock is uncontended, which costs nothing.
void savePortalConfig() {
    if (sdLock) xSemaphoreTakeRecursive(sdLock, portMAX_DELAY);
    saveConfig();
    if (sdLock) xSemaphoreGiveRecursive(sdLock);
}

void runSetupPortal(SetupPortal::Mode mode) {
    portal.begin(configDoc, savePortalConfig, mode);

    // Both conditions, not just isDone(): isDone() goes true the moment the
    // config is saved, ~1.2 s before loop() gets to ESP.restart(). Leaving on
    // isDone() alone skipped the restart and dropped straight into wifi.begin()
    // with the radio still in AP mode, which is what threw the flood of
    // ESP_ERR_WIFI_NOT_INIT.
    while (!portal.isDone() || portal.isRebootPending()) {
        portal.loop();
        delay(2);   // the radio and the TCP stack need the airtime
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


void onWiFiOffline(const char* why) {
    offlineRequested = true;
}

void onWiFiStatus(WiFiStatus status, const String& detail) {
    switch (status) {
        case WiFiStatus::Connected:
            // ASCII: the serial terminal shows anything else as question marks.
            Serial.printf("WiFi connected - http://%s\n", detail.c_str());
            break;
        case WiFiStatus::Disconnected:
            // Silent on purpose. This fires when the sweep begin() runs at boot
            // comes back empty, which is not yet news: the retries in loop() are
            // about to have a go. The clip belongs to the two moments that mean
            // something — the retries running out, and someone pressing the
            // button and finding nothing there — and both go through
            // offlineRequested so it is said once, from one place.
            Serial.println("Wifi Disconnected");
        break;
        case WiFiStatus::Reconnecting:
            Serial.println("Wifi Reconnecting");
            break;
    }
}

// ---------------------------------------------------------------------------
// Button edge capture.
//
// loop() cannot be trusted to sample the pin. wifi.loop() blocks for up to
// _timeoutPerNetwork (5 s) per configured network inside _trySTA()'s connect
// wait, so a board that cannot reach its network freezes the loop for half of
// every 10 s window — and a 150 ms tap landing in that half was never seen at
// all, which is exactly how the button came to look dead while the loop logic
// was fine.
//
// The interrupt records edges whatever the loop is doing; loop() drains the ring
// when it next runs. Late is survivable, lost is not.
// ---------------------------------------------------------------------------

struct BtnEdge {
    uint32_t ms;
    bool     down;
};

// 8 is plenty: a double press is four edges, and anything faster than that is
// contact bounce the debounce below discards anyway.
volatile BtnEdge btnRing[8];
volatile uint8_t btnHead = 0;   // written by the ISR
volatile uint8_t btnTail = 0;   // written by loop()
volatile uint32_t btnDropped = 0;

void IRAM_ATTR onButtonEdge() {
    // esp_timer_get_time() and not millis(): millis() is not safe to call from
    // an ISR on this core.
    const uint32_t now  = (uint32_t)(esp_timer_get_time() / 1000);
    const uint8_t  next = (uint8_t)((btnHead + 1) % 8);

    if (next == btnTail) {          // ring full: loop() has been away too long
        ++btnDropped;
        return;
    }
    btnRing[btnHead].ms   = now;
    btnRing[btnHead].down = digitalRead(BUTTON_PIN) == LOW;
    btnHead               = next;
}

bool btnPop(BtnEdge& out) {
    if (btnTail == btnHead) return false;
    // Field by field: the implicit copy assignment takes a const&, which a
    // volatile source cannot bind to.
    out.ms   = btnRing[btnTail].ms;
    out.down = btnRing[btnTail].down;
    btnTail  = (uint8_t)((btnTail + 1) % 8);
    return true;
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

    // Before the WiFi stack and before the photo task: with no network to join
    // there is nothing for the rest of setup() to do, and the portal reboots the
    // board anyway, so anything started here would only be torn down again.
    if (needsSetup()) {
        Serial.println("No usable config: starting the onboarding portal.");
        runSetupPortal(SetupPortal::Mode::Onboarding);
    }

    wifi.onStatusChange(onWiFiStatus);
    wifi.onOffline(onWiFiOffline);
    // Three sweeps of every saved network, then stop and ask for help rather
    // than reconnecting into the void for ever.
    wifi.setMaxReconnectSweeps(3);
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
    // detail than describing a room needs.
    //
    // SVGA is also exactly the largest of these the backend leaves alone.
    // app/images.py shrinks the long side to IMAGE_MAX_SIDE, 896 px by default,
    // and returns early only when max(width, height) <= 896 — so 800x600 goes
    // straight through while XGA's 1024 does not. Its own benchmarks put that
    // resize at 192 ms, paid on top of uploading pixels that are then thrown
    // away. Anything above SVGA costs time twice and buys nothing.
    //
    // So this tracks IMAGE_MAX_SIDE, not the camera's capabilities: if that is
    // raised on the server, the next size up becomes worth sending. Override per
    // board with "frame_size".
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

    // A pulled-up pin with the switch idle must read HIGH. A LOW here means the
    // line is held to ground — a bodge wire touching something, or a stuck
    // switch — and no amount of loop() logic makes a press detectable.
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonEdge, CHANGE);
    Serial.printf("Button on GPIO%d: resting level %s (expected HIGH)\n",
                  BUTTON_PIN, digitalRead(BUTTON_PIN) ? "HIGH" : "LOW");

    if(isFirstBoot) {
        audio.playDefault(DefaultAudios::FIRST_BOOT);
    }
}

void loop() {
    // Out of network: say so out loud and put the panel up, so the way to fix
    // it is the same whether the retries ran out or a button press just found
    // there was nothing there. Checked first — this is the board announcing it
    // cannot do its job, and everything below assumes it can.
    if (offlineRequested) {
        offlineRequested = false;
        Serial.println("No network: playing the clip and raising the panel.");
        audio.playDefault(DefaultAudios::NO_WIFI);
        runSetupPortal(SetupPortal::Mode::Panel);
    }

    // Button: debounce, then single / double / long press.
    //
    // Presses are counted on release rather than on the falling edge, which is
    // what makes a long press distinguishable at all: while the button is still
    // down there is no way to know yet whether it is a tap. It costs look() the
    // duration of the press itself — some 80 ms on top of the 250 ms the
    // double-press window already cost — and buys the AP an entry point on a
    // board with no screen and no console.
    static bool     held          = false;
    static uint32_t lastChange    = 0;
    static uint32_t pressStart    = 0;   // falling edge of the press being held
    static uint32_t lastRelease   = 0;   // rising edge, opens the double window
    static uint8_t  pressCount    = 0;
    static bool     waitingDouble = false;
    static bool     longFired     = false;

    const uint32_t DEBOUNCE_MS     = 50;
    const uint32_t DOUBLE_PRESS_MS = 250;
    // 2 s: past anything anyone reaches while tapping, short enough to find
    // without being told.
    const uint32_t LONG_PRESS_MS   = 2000;

    uint32_t now = millis();

    // Drain every edge the ISR recorded, including ones from while the loop was
    // blocked. Timestamps come from the edge, not from now, so a press that is
    // only read about seconds later is still measured correctly.
    BtnEdge e;
    while (btnPop(e)) {
        if (e.down == held) continue;                  // no transition
        if (e.ms - lastChange <= DEBOUNCE_MS) continue; // contact bounce
        lastChange = e.ms;
        held       = e.down;

        if (held) {
            pressStart = e.ms;
            longFired  = false;
            Serial.printf("BUTTON pressed on GPIO%d\n", BUTTON_PIN);
        } else {
            lastRelease = e.ms;
            // A press already consumed by the long-press branch is not a tap.
            if (!longFired) {
                pressCount++;
                waitingDouble = true;
            }
        }
    }

    if (btnDropped) {
        Serial.printf("BUTTON dropped %u edge(s): the loop was away too long\n",
                      (unsigned)btnDropped);
        btnDropped = 0;
    }

    // Fires while the button is still down, so holding it does something
    // observable instead of the user having to guess how long is long enough.
    // Never returns: the portal ends in a reboot.
    if (held && !longFired && now - pressStart >= LONG_PRESS_MS) {
        longFired     = true;
        waitingDouble = false;
        pressCount    = 0;
        Serial.println("BUTTON held: raising the setup access point.");
        // Synthesised rather than a clip: this has to work on a board whose card
        // is empty or missing, which is exactly the board someone reaches for
        // this button on.
        audio.beep(880, 220);
        runSetupPortal(SetupPortal::Mode::Panel);
    }

    if (waitingDouble && now - lastRelease > DOUBLE_PRESS_MS) {
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
