#include "WiFiManager.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Lang.h>   // l'idioma el té main.cpp

bool WiFiManager::loadNetworksFromJson(JsonDocument& doc, String ssids[], String passes[], uint32_t& count) {
    JsonObject wifiObj = doc["wifi"];
    if (wifiObj.isNull()) {
        count = 0;
        return false;
    }

    count = wifiObj["count"] | 0;
    if (count > WIFI_MAX_NETWORKS) {
        count = WIFI_MAX_NETWORKS;
    }

    for (uint32_t i = 0; i < count; ++i) {
        ssids[i]  = wifiObj["ssid" + String(i)] | "";
        passes[i] = wifiObj["pass" + String(i)] | "";
    }

    return count > 0;
}

bool WiFiManager::saveNetworksToJson(JsonDocument& doc, const String ssids[], const String passes[], uint32_t count) {
    JsonObject wifiObj = doc["wifi"];
    if (wifiObj.isNull()) {
        wifiObj = doc.createNestedObject("wifi");
    } else {
        wifiObj.clear();
    }

    wifiObj["count"] = count;
    for (uint32_t i = 0; i < count; ++i) {
        wifiObj["ssid" + String(i)] = ssids[i];
        wifiObj["pass" + String(i)] = passes[i];
    }

    return true;
}

void WiFiManager::addNetwork(const String& ssid, const String& password) {
    if (!_config) return;

    String ssids[WIFI_MAX_NETWORKS];
    String passes[WIFI_MAX_NETWORKS];
    uint32_t count = 0;

    if (loadNetworksFromJson(*_config, ssids, passes, count)) {
        for (uint32_t i = 0; i < count; ++i) {
            if (ssids[i] == ssid) {
                passes[i] = password;
                saveNetworksToJson(*_config, ssids, passes, count);
                if (_onChanged) _onChanged();
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
    saveNetworksToJson(*_config, ssids, passes, count);
    if (_onChanged) _onChanged();
}

void WiFiManager::removeNetwork(const String& ssid) {
    if (!_config) return;

    String ssids[WIFI_MAX_NETWORKS];
    String passes[WIFI_MAX_NETWORKS];
    uint32_t count = 0;

    if (!loadNetworksFromJson(*_config, ssids, passes, count)) {
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

    saveNetworksToJson(*_config, ssids, passes, writeIdx);
    if (_onChanged) _onChanged();
}

void WiFiManager::begin(uint32_t timeoutPerNetworkMs) {
    _timeoutPerNetwork = timeoutPerNetworkMs;

    if (!_tryAllNetworks()) {
        _notify(WiFiStatus::Disconnected);
    }
}

bool WiFiManager::_tryAllNetworks() {
    if (!_config) {
        return false;
    }

    String ssids[WIFI_MAX_NETWORKS];
    String passes[WIFI_MAX_NETWORKS];
    uint32_t count = 0;

    if (!loadNetworksFromJson(*_config, ssids, passes, count)) {
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

    _notify(WiFiStatus::Connected, WiFi.localIP().toString());
    return true;
}

void WiFiManager::loop() {
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


/*
    TODO: API
*/

// Root store the core already ships, so HTTPS validates with no CA in config.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[]   asm("_binary_x509_crt_bundle_end");

namespace {

// The WAV is held in RAM whole. first_boot alone is 379 KB at 16 kHz, so this
// only fits because the board has 8 MB of PSRAM; longer clips need streaming.
constexpr int      kMaxAudioBytes  = 512 * 1024;
constexpr uint16_t kHttpTimeoutMs  = 15000;

// Byte-by-byte, which is what UTF-8 needs: accents are more than one byte.
String urlEncode(const String& cru) {
    static const char* hex = "0123456789ABCDEF";
    String fora;
    fora.reserve(cru.length() * 3);
    for (size_t i = 0; i < cru.length(); ++i) {
        const uint8_t c = static_cast<uint8_t>(cru[i]);
        const bool segur = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                        || (c >= '0' && c <= '9')
                        || c == '-' || c == '_' || c == '.' || c == '~';
        if (segur) {
            fora += static_cast<char>(c);
        } else {
            fora += '%';
            fora += hex[c >> 4];
            fora += hex[c & 0x0F];
        }
    }
    return fora;
}

}  // namespace

// Shared by textToSpeech() and defaultAudio(): both only differ in the URL.
String WiFiManager::_getAudio(const String& ruta, const char* qui) {
    if (!_config) return "";
    if (!_config->containsKey("backend_url")) return "";
    String backendUrl = (*_config)["backend_url"] | "";
    if (backendUrl.length() == 0) return "";
    if (!isConnected()) return "";

    while (backendUrl.endsWith("/")) backendUrl.remove(backendUrl.length() - 1);
    const String url = backendUrl + ruta;

    WiFiClient       client;       //http
    WiFiClientSecure secureClient; //https
    const bool isHttps = backendUrl.startsWith("https://");
    if (isHttps) {
        // Same roots a browser trusts, so Let's Encrypt validates out of the
        // box. "backend_ca" in the config pins a certificate instead.
        const char* ca = (*_config)["backend_ca"] | "";
        if (strlen(ca) > 0) secureClient.setCACert(ca);
        else                secureClient.setCACertBundle(
                                rootca_crt_bundle_start,
                                rootca_crt_bundle_end - rootca_crt_bundle_start);
    }

    HTTPClient http;
    http.setConnectTimeout(kHttpTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);
    if (!(isHttps ? http.begin(secureClient, url) : http.begin(client, url))) {
        Serial.printf("%s: invalid URL\n", qui);
        return "";
    }

    const char* token = (*_config)["api_token"] | "";
    if (strlen(token) > 0) http.addHeader("X-API-Token", token);

    const int codi = http.GET();
    if (codi != HTTP_CODE_OK) {
        // On a 4xx the body is {"detail": "..."} saying what went wrong.
        Serial.printf("%s: HTTP %d %s\n", qui, codi,
                      http.errorToString(codi).c_str());
        if (codi > 0) Serial.println(http.getString());
        http.end();
        return "";
    }

    // With wav the backend always sends Content-Length.
    const int mida = http.getSize();
    if (mida > kMaxAudioBytes) {
        Serial.printf("%s: %d too many bytes (limit %d)\n", qui, mida, kMaxAudioBytes);
        http.end();
        return "";
    }
    // Better to say so than to die on a failed malloc.
    if (mida > 0 && static_cast<int>(ESP.getMaxAllocHeap()) < mida + 1) {
        Serial.printf("%s: %d contiguous bytes are needed and the largest "
                      "remaining block is %u\n", qui, mida + 1, ESP.getMaxAllocHeap());
        http.end();
        return "";
    }

    // getString() memcpys and keeps the length apart, so the 0x00 bytes of the
    // PCM survive. Callers must use length(), not c_str().
    String wav = http.getString();
    http.end();

    if (mida > 0 && static_cast<int>(wav.length()) != mida) {
        // Connection cut mid-download: half a WAV is no use.
        Serial.printf("%s: %u bytes arrived out of the %d the server announced\n",
                      qui, wav.length(), mida);
        return "";
    }
    if (wav.length() < 44 || !wav.startsWith("RIFF")) {
        Serial.printf("%s: this is not a WAV (%u bytes)\n", qui, wav.length());
        return "";
    }
    return wav;
}

String WiFiManager::textToSpeech(const String text) {
    if (text.length() == 0) return "";
    // GET, not POST: /speak changes nothing and it all fits in the query.
    // wav for the header, 16 kHz for the MAX98357A's I2S.
    return _getAudio(String("/api/v1/speak?text=") + urlEncode(text)
                     + "&lang=" + bonsai::lang()
                     + "&audioFormat=wav&sampleRate=16000",
                     "textToSpeech");
}

String WiFiManager::defaultAudio(const String& clipId) {
    if (clipId.length() == 0) return "";
    // No text here: the backend decides what each clip says (clips.py).
    return _getAudio(String("/api/v1/clips/") + urlEncode(clipId)
                     + "?lang=" + bonsai::lang()
                     + "&audioFormat=wav&sampleRate=16000",
                     "defaultAudio");
}
