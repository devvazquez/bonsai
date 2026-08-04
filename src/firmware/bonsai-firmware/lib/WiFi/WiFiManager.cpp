#include "WiFiManager.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace {
constexpr const char* kConfigFile = "/wifi_config.json";
}

void WiFiManager::addNetwork(const String& ssid, const String& password) {
    String ssids[WIFI_MAX_NETWORKS];
    String passes[WIFI_MAX_NETWORKS];
    uint32_t count = 0;

    if (_loadNetworksFromSD(ssids, passes, count)) {
        for (uint32_t i = 0; i < count; ++i) {
            if (ssids[i] == ssid) {
                passes[i] = password;
                _saveNetworksToSD(ssids, passes, count);
                return;
            }
        }
    } else {
        count = 0;
    }

    if (count >= WIFI_MAX_NETWORKS) {
        return;
    }

    ssids[count] = ssid;
    passes[count] = password;
    ++count;
    _saveNetworksToSD(ssids, passes, count);
}

void WiFiManager::removeNetwork(const String& ssid) {
    String ssids[WIFI_MAX_NETWORKS];
    String passes[WIFI_MAX_NETWORKS];
    uint32_t count = 0;

    if (!_loadNetworksFromSD(ssids, passes, count)) {
        return;
    }

    uint32_t writeIdx = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (ssids[i] != ssid) {
            ssids[writeIdx] = ssids[i];
            passes[writeIdx] = passes[i];
            ++writeIdx;
        }
    }

    _saveNetworksToSD(ssids, passes, writeIdx);
}

void WiFiManager::begin(uint32_t timeoutPerNetworkMs) {
    _audio = Audio();
    _timeoutPerNetwork = timeoutPerNetworkMs;

    if (!_tryAllNetworks()) {
        _audio.playDefault(DefaultAudios::NO_WIFI);
    }
}

bool WiFiManager::_tryAllNetworks() {
    String ssids[WIFI_MAX_NETWORKS];
    String passes[WIFI_MAX_NETWORKS];
    uint32_t count = 0;

    if (!_loadNetworksFromSD(ssids, passes, count)) {
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (ssids[i].length() == 0) {
            continue;
        }

        if (_trySTA(ssids[i].c_str(), passes[i].c_str())) {
            return true;
        }
    }

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

    _startServer();
    _notify(WiFiStatus::Connected, WiFi.localIP().toString());
    return true;
}

void WiFiManager::_startServer() {
    if (!_serverStarted) {
        _server.begin();
        _serverStarted = true;
    }
}

void WiFiManager::loop() {
    if (_serverStarted) _server.handleClient();

    if (millis() - _lastCheckMs < 10000) return;
    _lastCheckMs = millis();

    if (WiFi.status() != WL_CONNECTED) {
        _notify(WiFiStatus::Reconnecting);
        _tryAllNetworks();
    }
}

bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

String WiFiManager::localIP() const {
    return WiFi.localIP().toString();
}

void WiFiManager::_notify(WiFiStatus status, const String& detail) {
    if (_statusCallback) _statusCallback(status, detail);
}

bool WiFiManager::_loadNetworksFromSD(String ssids[], String passes[], uint32_t& count) {
    if (!SD.exists(kConfigFile)) {
        count = 0;
        return false;
    }

    File file = SD.open(kConfigFile, FILE_READ);
    if (!file) {
        count = 0;
        return false;
    }

    String content = file.readString();
    file.close();

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, content);
    if (error) {
        count = 0;
        return false;
    }

    count = doc["count"] | 0;
    if (count > WIFI_MAX_NETWORKS) {
        count = WIFI_MAX_NETWORKS;
    }

    for (uint32_t i = 0; i < count; ++i) {
        String ssidKey = "ssid" + String(i);
        String passKey = "pass" + String(i);

        ssids[i] = doc[ssidKey] | "";
        passes[i] = doc[passKey] | "";
    }

    return true;
}

bool WiFiManager::_saveNetworksToSD(const String ssids[], const String passes[], uint32_t count) {
    DynamicJsonDocument doc(2048);
    doc["count"] = count;

    for (uint32_t i = 0; i < count; ++i) {
        String ssidKey = "ssid" + String(i);
        String passKey = "pass" + String(i);
        doc[ssidKey] = ssids[i];
        doc[passKey] = passes[i];
    }

    File file = SD.open(kConfigFile, FILE_WRITE);
    if (!file) {
        return false;
    }
    serializeJson(doc, file);
    file.close();
    return true;
}
