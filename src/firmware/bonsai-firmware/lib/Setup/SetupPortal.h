#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <functional>

// Setup over WiFi: the device becomes an access point and serves a page that
// posts language, networks, backend and tokens back into /config.json.
//
// Two pages, one server. Which one depends on why the AP came up:
//
//     Onboarding  bonsai-setup.html — the first-boot walkthrough, one step at a
//                 time, for someone holding a board that has never worked yet.
//     Panel       bonsai-panel.html — everything on one screen, prefilled from
//                 the running config, for changing a password on a board that is
//                 already set up. Reached by holding the button.
//
// Both are embedded in flash (lib/Setup/setup_page.h and panel_page.h, generated
// from the HTML at build time), so setup works with a blank SD card.
//
//     SetupPortal portal;
//     portal.begin(configDoc, saveConfig, SetupPortal::Mode::Panel);
//     while (!portal.isDone() || portal.isRebootPending()) portal.loop();
//
// Endpoints, matching what the pages' fetch() calls expect:
//     GET  /              the page for the current mode
//     GET  /api/scan      [{ssid, rssi, secure}, ...]
//     GET  /api/config    the current config, secrets replaced by a flag
//     POST /api/test      {backend_url, api_token, groq_api_key}
//     POST /api/config    {lang, backend_url, api_token, groq_api_key, wifi{}}
class SetupPortal {
public:
    using SaveCallback = std::function<void()>;

    enum class Mode {
        Onboarding,   // first boot: the step-by-step page
        Panel,        // reconfiguring a working board: the flat page
    };

    // Opens the AP and starts serving. `config` is the same document main.cpp
    // owns; the portal writes into it and calls `onSave` to persist it.
    void begin(JsonDocument& config, SaveCallback onSave,
               Mode mode = Mode::Onboarding);

    // Pump both servers. Call as fast as the loop allows.
    void loop();

    // True once the config has been saved. The portal restarts the board
    // shortly afterwards, so this only goes true for the last moment or two.
    bool isDone() const { return _saved; }

    // True between the save and the restart. isDone() goes true first, so a
    // caller that only watches isDone() leaves the loop before loop() ever
    // reaches ESP.restart() and the board carries on with the AP still up.
    bool isRebootPending() const { return _rebootAtMs != 0; }

    // "Bonsai-A4C1" — the network the user looks for. Valid after begin().
    String apSsid() const { return _apSsid; }

private:
    WebServer   _server{80};
    DNSServer   _dns;
    JsonDocument* _config = nullptr;
    SaveCallback  _onSave;

    String   _apSsid;
    Mode     _mode       = Mode::Onboarding;
    bool     _saved      = false;
    uint32_t _rebootAtMs = 0;

    void _handlePage();
    void _handleScan();
    void _handleTest();
    void _handleConfig();
    void _handleConfigGet();
    void _handleNotFound();

    // 404 unless the body parses; leaves the parsed request in `doc`.
    bool _readJsonBody(JsonDocument& doc);
};
