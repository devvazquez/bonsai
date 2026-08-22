#include "SetupPortal.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <esp_mac.h>

#include "setup_page.h"
#include "panel_page.h"

namespace {

// Open network on purpose: a WPA2 password would have to be printed somewhere
// for the user to type, and the AP only exists until the config is saved.
constexpr const char* kApPassword = nullptr;
constexpr uint8_t     kApChannel  = 1;
constexpr int         kApMaxConn  = 2;

// Enough for the largest POST: 5 networks plus the URL and two tokens.
constexpr size_t kRequestDocSize = 4096;

// A crowded flat sees plenty of networks but the list is only there to be
// tapped, so it is capped rather than sized for every last one.
constexpr uint32_t kMaxScanResults = 30;
constexpr size_t   kScanDocSize    = 6144;

constexpr uint32_t kRebootDelayMs = 1200;  // let the response reach the browser
constexpr uint16_t kProbeTimeoutMs = 8000;

// The CA bundle the core ships, same as WiFiManager uses, so an https backend
// validates during the connection test too.
extern "C" {
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[]   asm("_binary_x509_crt_bundle_end");
}

// "Bonsai-A4C1" from the last two bytes of the factory MAC, so two boards in
// the same room do not offer the same network.
String apName() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char buf[16];
    snprintf(buf, sizeof(buf), "Bonsai-%02X%02X", mac[4], mac[5]);
    return String(buf);
}

}  // namespace


void SetupPortal::begin(JsonDocument& config, SaveCallback onSave, Mode mode) {
    _config = &config;
    _onSave = std::move(onSave);
    _mode   = mode;

    // AP_STA and not AP: /api/scan needs the station side to see other networks.
    WiFi.mode(WIFI_AP_STA);

    // Whether the station is actually connected, not which mode we are in.
    //
    // A live association is worth keeping: it is what lets /api/test reach the
    // backend instead of answering "ap_mode", which is the only thing the test
    // button is for. A station that is merely *trying*, though, has to go. It
    // keeps the radio scanning off-channel for a network that is not there, the
    // access point's beacons go out only between those scans, and the phone
    // never sees it in its list — which is exactly the state the panel comes up
    // in when the retries have just run out.
    const bool staLive = WiFi.status() == WL_CONNECTED;
    if (!staLive) WiFi.disconnect(true);

    const String ssid = apName();
    if (!WiFi.softAP(ssid.c_str(), kApPassword, kApChannel, false, kApMaxConn)) {
        // Worth failing loudly: without this the line below cheerfully announces
        // a network to join that was never created, and the only symptom is
        // someone staring at their phone.
        Serial.println("Setup portal: softAP failed, there is no network to join");
        return;
    }
    _apSsid = ssid;

    const IPAddress ip = WiFi.softAPIP();

    // Everything resolves to us, so phones open the page by themselves instead
    // of the user having to type an address.
    _dns.setErrorReplyCode(DNSReplyCode::NoError);
    _dns.start(53, "*", ip);

    _server.on("/",            HTTP_GET,  [this] { _handlePage(); });
    _server.on("/api/scan",    HTTP_GET,  [this] { _handleScan(); });
    _server.on("/api/test",    HTTP_POST, [this] { _handleTest(); });
    _server.on("/api/config",  HTTP_GET,  [this] { _handleConfigGet(); });
    _server.on("/api/config",  HTTP_POST, [this] { _handleConfig(); });
    _server.onNotFound([this] { _handleNotFound(); });
    _server.begin();

    // Channel and station state included on purpose: "I cannot see the network"
    // is otherwise indistinguishable from "the AP never came up", and those have
    // nothing to do with each other.
    Serial.printf("Setup portal up (%s): join \"%s\" on channel %d, "
                  "open http://%s/ (station %s)\n",
                  mode == Mode::Panel ? "panel" : "onboarding",
                  ssid.c_str(), WiFi.channel(), ip.toString().c_str(),
                  staLive ? "kept, connected" : "stopped");
}

void SetupPortal::loop() {
    _dns.processNextRequest();
    _server.handleClient();

    if (_rebootAtMs != 0 && millis() >= _rebootAtMs) {
        Serial.println("Config saved, restarting.");
        _server.stop();
        _dns.stop();
        WiFi.softAPdisconnect(true);
        delay(50);
        ESP.restart();
    }
}

void SetupPortal::_handlePage() {
    // send_P streams straight out of flash; the pages are tens of KB and
    // copying one into a String first would be a waste of heap.
    _server.sendHeader("Cache-Control", "no-store");
    if (_mode == Mode::Panel) {
        _server.send_P(200, "text/html; charset=utf-8", kPanelPage, kPanelPageLen);
    } else {
        _server.send_P(200, "text/html; charset=utf-8", kSetupPage, kSetupPageLen);
    }
}

// What the panel prefills itself from. No secrets go out over an open access
// point: the tokens and the WiFi passwords are reported as a boolean saying
// whether one is stored, which is all the page needs to show a filled-in field
// it can leave alone.
void SetupPortal::_handleConfigGet() {
    DynamicJsonDocument doc(kRequestDocSize);

    if (_config) {
        doc["lang"]        = (*_config)["lang"]        | "";
        doc["backend_url"] = (*_config)["backend_url"] | "";
        doc["frame_size"]  = (*_config)["frame_size"]  | "svga";

        const char* token = (*_config)["api_token"]    | "";
        const char* groq  = (*_config)["groq_api_key"] | "";
        doc["has_api_token"]    = token[0] != '\0';
        doc["has_groq_api_key"] = groq[0]  != '\0';

        String   ssids[WIFI_MAX_NETWORKS];
        String   passes[WIFI_MAX_NETWORKS];
        uint32_t count = 0;
        WiFiManager::loadNetworksFromJson(*_config, ssids, passes, count);

        // "networks" and not "wifi": the POST takes the flat {count, ssid0,
        // pass0, ...} object WiFiManager reads, and reusing the name for an
        // array of objects here would be asking for one to be sent as the other.
        JsonArray nets = doc.createNestedArray("networks");
        for (uint32_t i = 0; i < count; ++i) {
            JsonObject net = nets.createNestedObject();
            net["ssid"]         = ssids[i];
            net["has_password"] = passes[i].length() > 0;
        }
    }

    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}

void SetupPortal::_handleScan() {
    // Synchronous scan: the page has already put a spinner up and there is
    // nothing else for the device to be doing.
    const int found = WiFi.scanNetworks(false, false);

    // Built with ArduinoJson rather than by hand so that a quote or a backslash
    // in an SSID cannot break the response.
    DynamicJsonDocument doc(kScanDocSize);
    JsonArray           arr = doc.to<JsonArray>();

    uint32_t added = 0;
    for (int i = 0; i < found && added < kMaxScanResults; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;  // hidden, nothing to show the user

        JsonObject net = arr.createNestedObject();
        net["ssid"]   = ssid;
        net["rssi"]   = WiFi.RSSI(i);
        net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        ++added;
    }

    WiFi.scanDelete();

    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}

void SetupPortal::_handleTest() {
    DynamicJsonDocument req(kRequestDocSize);
    if (!_readJsonBody(req)) return;

    const String url   = req["backend_url"] | "";
    const String token = req["api_token"]   | "";

    if (url.length() == 0) {
        _server.send(400, "application/json",
                     "{\"ok\":false,\"reason\":\"no_url\"}");
        return;
    }

    // The device is an access point with no uplink of its own: until it has
    // joined one of the networks the user just entered there is no route to the
    // backend. Saying so beats reporting a bad address.
    if (WiFi.status() != WL_CONNECTED) {
        _server.send(503, "application/json",
                     "{\"ok\":false,\"reason\":\"ap_mode\"}");
        return;
    }

    String probe = url;
    while (probe.endsWith("/")) probe.remove(probe.length() - 1);
    probe += "/api/v1/audios";

    WiFiClient       plain;
    WiFiClientSecure secure;
    const bool isHttps = probe.startsWith("https://");
    if (isHttps) {
        secure.setCACertBundle(rootca_crt_bundle_start,
                               rootca_crt_bundle_end - rootca_crt_bundle_start);
    }

    HTTPClient http;
    http.setConnectTimeout(kProbeTimeoutMs);
    http.setTimeout(kProbeTimeoutMs);
    if (!(isHttps ? http.begin(secure, probe) : http.begin(plain, probe))) {
        _server.send(502, "application/json",
                     "{\"ok\":false,\"reason\":\"bad_url\"}");
        return;
    }
    if (token.length() > 0) http.addHeader("X-API-Token", token);

    const int code = http.GET();
    http.end();

    if (code == HTTP_CODE_OK) {
        _server.send(200, "application/json", "{\"ok\":true}");
    } else {
        String body = "{\"ok\":false,\"reason\":\"http\",\"status\":";
        body += code;
        body += "}";
        _server.send(502, "application/json", body);
    }
}

void SetupPortal::_handleConfig() {
    if (!_config) {
        _server.send(500, "application/json",
                     "{\"ok\":false,\"reason\":\"no_config\"}");
        return;
    }

    DynamicJsonDocument req(kRequestDocSize);
    if (!_readJsonBody(req)) return;

    // The page sends "ca-ES"; the config stores it that way and bonsai::lang()
    // is what trims it to "ca" for the backend.
    const String lang = req["lang"] | "";
    if (lang.length() > 0) (*_config)["lang"] = lang;

    // Only the keys the request actually carries. The panel leaves out a token
    // the user did not retype — it never had the value to send back — so writing
    // these unconditionally would wipe the credentials of a working board on
    // every save. A key present but empty still clears the field: absent and
    // empty mean different things here, deliberately.
    if (req.containsKey("backend_url"))
        (*_config)["backend_url"] = req["backend_url"] | "";
    if (req.containsKey("api_token"))
        (*_config)["api_token"] = req["api_token"] | "";
    if (req.containsKey("groq_api_key"))
        (*_config)["groq_api_key"] = req["groq_api_key"] | "";
    if (req.containsKey("frame_size"))
        (*_config)["frame_size"] = req["frame_size"] | "svga";

    // Same shape both ways, so the page's wifi block goes through the very
    // helpers WiFiManager reads it back with.
    String   ssids[WIFI_MAX_NETWORKS];
    String   passes[WIFI_MAX_NETWORKS];
    uint32_t count = 0;
    if (WiFiManager::loadNetworksFromJson(req, ssids, passes, count)) {
        // An empty password for an SSID that is already stored means "leave it
        // alone", not "make it an open network". The panel cannot send a password
        // back — it was never given one — so without this, saving a change to the
        // language would drop the WiFi credentials and brick the board until
        // someone set it up again from scratch.
        //
        // The cost is that a network cannot be turned from secured to open by
        // clearing its password: delete it and add it again instead. For an open
        // network the stored password is empty too, so reusing it changes nothing.
        String   oldSsids[WIFI_MAX_NETWORKS];
        String   oldPasses[WIFI_MAX_NETWORKS];
        uint32_t oldCount = 0;
        WiFiManager::loadNetworksFromJson(*_config, oldSsids, oldPasses, oldCount);

        for (uint32_t i = 0; i < count; ++i) {
            if (passes[i].length() > 0) continue;
            for (uint32_t j = 0; j < oldCount; ++j) {
                if (oldSsids[j] == ssids[i]) {
                    passes[i] = oldPasses[j];
                    break;
                }
            }
        }

        WiFiManager::saveNetworksToJson(*_config, ssids, passes, count);
    }

    if (_onSave) _onSave();

    Serial.printf("Setup saved: lang=%s, %u network(s), backend=%s\n",
                  ((*_config)["lang"] | "?"), count,
                  ((*_config)["backend_url"] | "(none)"));

    _server.send(200, "application/json", "{\"ok\":true}");

    // Reboot from loop() rather than here, so the response is actually on the
    // wire before the radio goes down.
    _saved      = true;
    _rebootAtMs = millis() + kRebootDelayMs;
}

void SetupPortal::_handleNotFound() {
    // Captive-portal probes (/generate_204, /hotspot-detect.html, ...) land
    // here. A redirect to the page is what makes the phone pop it open.
    _server.sendHeader("Location",
                       String("http://") + WiFi.softAPIP().toString() + "/");
    _server.send(302, "text/plain", "");
}

bool SetupPortal::_readJsonBody(JsonDocument& doc) {
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json",
                     "{\"ok\":false,\"reason\":\"no_body\"}");
        return false;
    }
    if (deserializeJson(doc, _server.arg("plain")) != DeserializationError::Ok) {
        _server.send(400, "application/json",
                     "{\"ok\":false,\"reason\":\"bad_json\"}");
        return false;
    }
    return true;
}
