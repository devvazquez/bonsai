#include "WiFiManager.h"
#include <HTTPClient.h>

// NVS layout (namespace "wifi"):
//   "count"  — uint32, number of saved networks
//   "ssid0"  — string, first SSID
//   "pass0"  — string, first password
//   "ssid1", "pass1", ... up to WIFI_MAX_NETWORKS

void WiFiManager::addNetwork(const String& ssid, const String& password) {
    Preferences prefs;
    prefs.begin("wifi", false);

    uint32_t count = prefs.getUInt("count", 0);

    // Check if SSID already exists — update password if so
    for (uint32_t i = 0; i < count; i++) {
        String key = "ssid" + String(i);
        if (prefs.getString(key.c_str(), "") == ssid) {
            prefs.putString(("pass" + String(i)).c_str(), password);
            prefs.end();
            return;
        }
    }

    if (count >= WIFI_MAX_NETWORKS) {
        prefs.end();
        return;
    }

    prefs.putString(("ssid" + String(count)).c_str(), ssid);
    prefs.putString(("pass" + String(count)).c_str(), password);
    prefs.putUInt("count", count + 1);
    prefs.end();
}

void WiFiManager::removeNetwork(const String& ssid) {
    Preferences prefs;
    prefs.begin("wifi", false);

    uint32_t count = prefs.getUInt("count", 0);
    uint32_t writeIdx = 0;

    String ssids[WIFI_MAX_NETWORKS];
    String passes[WIFI_MAX_NETWORKS];

    // Collect all except the one to remove
    for (uint32_t i = 0; i < count; i++) {
        String s = prefs.getString(("ssid" + String(i)).c_str(), "");
        String p = prefs.getString(("pass" + String(i)).c_str(), "");
        if (s != ssid) {
            ssids[writeIdx] = s;
            passes[writeIdx] = p;
            writeIdx++;
        }
    }

    // Rewrite compacted list
    for (uint32_t i = 0; i < writeIdx; i++) {
        prefs.putString(("ssid" + String(i)).c_str(), ssids[i]);
        prefs.putString(("pass" + String(i)).c_str(), passes[i]);
    }
    prefs.putUInt("count", writeIdx);
    prefs.end();
}

void WiFiManager::begin(uint32_t timeoutPerNetworkMs) {
    _timeoutPerNetwork = timeoutPerNetworkMs;
    _registerRoutes();

    if (!_tryAllNetworks()) {
        _startAP();
    }
}

bool WiFiManager::_tryAllNetworks() {
    Preferences prefs;
    prefs.begin("wifi", true);
    uint32_t count = prefs.getUInt("count", 0);

    for (uint32_t i = 0; i < count; i++) {
        String ssid = prefs.getString(("ssid" + String(i)).c_str(), "");
        String pass = prefs.getString(("pass" + String(i)).c_str(), "");
        if (ssid.length() == 0) continue;

        if (_trySTA(ssid.c_str(), pass.c_str())) {
            prefs.end();
            return true;
        }
    }

    prefs.end();
    return false;
}

bool WiFiManager::_trySTA(const char* ssid, const char* password) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start >= _timeoutPerNetwork) {
            WiFi.disconnect(true);
            return false;
        }
        delay(100);
    }

    _inAPMode = false;
    _startServer();
    _notify(WiFiStatus::Connected, WiFi.localIP().toString());
    return true;
}

void WiFiManager::_startAP() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    _inAPMode = true;
    _startServer();
    _notify(WiFiStatus::APStarted, WiFi.softAPIP().toString());
}

void WiFiManager::_startServer() {
    if (!_serverStarted) {
        _server.begin();
        _serverStarted = true;
    }
}

void WiFiManager::loop() {
    if (_serverStarted) _server.handleClient();

    // Check connection every 10s
    if (millis() - _lastCheckMs < 10000) return;
    _lastCheckMs = millis();

    if (!_inAPMode && WiFi.status() != WL_CONNECTED) {
        _notify(WiFiStatus::Reconnecting);
        if (!_tryAllNetworks()) {
            _startAP();
        }
    }
}

bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

String WiFiManager::localIP() const {
    if (_inAPMode) return WiFi.softAPIP().toString();
    return WiFi.localIP().toString();
}

void WiFiManager::_notify(WiFiStatus status, const String& detail) {
    if (_statusCallback) _statusCallback(status, detail);
}

void WiFiManager::_registerRoutes() {
    _server.on("/photos", [this]() { _handleList(); });
    _server.on("/photo",  [this]() { _handleFile(); });
    _server.onNotFound(   [this]() { _handleNotFound(); });
}

void WiFiManager::_handleList() {
    File root = SD.open("/");
    if (!root) { _server.send(500, "text/plain", "SD error"); return; }

    String json = "[";
    bool first = true;
    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String name = entry.name();
            if (name.endsWith(".jpg") || name.endsWith(".jpeg")) {
                if (!first) json += ",";
                json += "\"" + name + "\"";
                first = false;
            }
        }
        entry = root.openNextFile();
    }
    json += "]";
    _server.send(200, "application/json", json);
}

void WiFiManager::_handleFile() {
    if (!_server.hasArg("name")) {
        _server.send(400, "text/plain", "Missing name parameter");
        return;
    }
    File file = SD.open("/" + _server.arg("name"));
    if (!file) { _server.send(404, "text/plain", "File not found"); return; }
    _server.streamFile(file, "image/jpeg");
    file.close();
}

void WiFiManager::_handleNotFound() {
    _server.send(404, "text/plain", "Not found");
}

void WiFiManager::streamTTS(const String& text, const char* apiKey, const char* voiceId,
                             std::function<void(const uint8_t*, size_t)> onChunk) {
    HTTPClient http;
    String url = String("https://api.elevenlabs.io/v1/text-to-speech/") + voiceId + "/stream";
    http.begin(url);
    http.addHeader("xi-api-key",   apiKey);
    http.addHeader("Content-Type", "application/json");

    String body = "{\"text\":\"" + text + "\","
                  "\"model_id\":\"eleven_multilingual_v2\","
                  "\"output_format\":\"pcm_16000\"}";

    int code = http.POST(body);
    if (code != HTTP_CODE_OK) {
        Serial.printf("[WiFi] streamTTS failed: %d\n", code);
        http.end();
        return;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[512];
    while (http.connected() || stream->available()) {
        size_t avail = stream->available();
        if (avail) {
            size_t n = stream->readBytes(buf, min(avail, (size_t)512));
            onChunk(buf, n);
        }
    }
    http.end();
}
