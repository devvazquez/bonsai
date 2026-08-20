#include "WiFiManager.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Lang.h>   // l'idioma el té main.cpp
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>

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
    // mode() first, and disconnect() without turning the radio off. Both matter:
    //
    // disconnect() on a stack that has not been brought up yet returns
    // ESP_ERR_WIFI_NOT_INIT and leaves the driver somewhere the begin() below
    // cannot recover from, which then reports ESP_ERR_WIFI_NOT_STARTED. And the
    // `true` argument powers the radio down, so the retry from loop() came back
    // to a stack that was off and failed the same way again — a board with
    // perfectly good credentials sat printing "Wifi Reconnecting" for ever.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    WiFi.begin(ssid, password);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start >= _timeoutPerNetwork) {
            WiFi.disconnect(false);
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

// Ceiling for the String path only, and the reason the longer clips could not
// be fetched at all: Arduino String allocates with realloc(), which comes from
// internal heap and never touches PSRAM, so the 8 MB on this board does not
// help. first_boot is 391 KB against a largest free block of about 120 KB, and
// no amount of retrying changes that. Anything that has to survive being large
// goes through the file path below instead.
constexpr int      kMaxAudioBytes  = 512 * 1024;

// The file path writes straight to the card, so the only real limit is the
// card. This is a sanity bound, not a memory one.
constexpr int      kMaxStreamBytes = 8 * 1024 * 1024;

constexpr uint16_t kHttpTimeoutMs  = 15000;
// 4 KB rather than 2: fewer round trips through mbedTLS and the VFS for the
// same bytes, and it still sits comfortably in internal RAM as a stack buffer.
constexpr size_t   kStreamChunk    = 4096;

// How long to sit with an open connection that has stopped producing bytes
// before calling it dead. Without this a half-finished response hangs the loop.
constexpr uint32_t kStallTimeoutMs = 10000;

// Shorter for the audio sink: a gap in a sentence being spoken means it is over,
// and the amplifier is holding GPIO8 — and with it the card's MISO — for every
// millisecond of the wait.
constexpr uint32_t kSinkStallMs    = 2500;

// The transfer buffer belongs on the heap, not the stack. The loopTask gets
// 8 KB by default and mbedTLS wants a good part of it, so a 4 KB array here
// plus whatever the sink callback needs below it overflows — which is exactly
// what a stack canary panic in loopTask looks like.
struct HeapBuf {
    uint8_t* p;
    explicit HeapBuf(size_t n) : p(static_cast<uint8_t*>(malloc(n))) {}
    ~HeapBuf() { free(p); }
    HeapBuf(const HeapBuf&) = delete;
    HeapBuf& operator=(const HeapBuf&) = delete;
};

// A Stream that exists only to be handed to HTTPClient::writeToStream(), which
// forwards the body here in pieces.
//
// Worth the indirection: reading getStreamPtr() directly returns the body raw,
// and a chunked response is framed with hex lengths and CRLFs that would land
// in the middle of the PCM as bursts of noise. writeToStream() unwraps that,
// and it knows where the body ends — the hand-rolled loop had to guess with a
// stall timeout, and paid 2.5 s of silence for the guess every time.
class SinkStream : public Stream {
public:
    explicit SinkStream(const WiFiManager::AudioSink* sink) : _sink(sink) {}

    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t* data, size_t len) override {
        if (_sink && _sink->onChunk && !_sink->onChunk(data, len)) return 0;
        _total += len;
        return len;
    }

    // Write-only: nothing ever reads from this end.
    int  available() override { return 0; }
    int  read() override      { return -1; }
    int  peek() override      { return -1; }
    void flush() override     {}

    size_t total() const { return _total; }

private:
    const WiFiManager::AudioSink* _sink;
    size_t                        _total = 0;
};

// Takes the card lock for a scope and always gives it back, including on the
// several early returns in the streaming loop below.
struct SdGuard {
    SemaphoreHandle_t h;
    explicit SdGuard(SemaphoreHandle_t lock) : h(lock) {
        if (h) xSemaphoreTakeRecursive(h, portMAX_DELAY);
    }
    ~SdGuard() { if (h) xSemaphoreGiveRecursive(h); }
};

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

// One request, consumed one of two ways. Exactly one of outStr/outFile is set:
// a String for the short clips, or straight to the card for anything that would
// not fit in internal heap. A body makes it a POST.
bool WiFiManager::_request(const String& ruta, const char* qui,
                          const uint8_t* body, size_t bodyLen,
                          String* outStr, File* outFile, const AudioSink* sink,
                          String* textHeader) {
    if (!_config) return false;
    if (!_config->containsKey("backend_url")) return false;
    String backendUrl = (*_config)["backend_url"] | "";
    if (backendUrl.length() == 0) {
        Serial.printf("%s: backend_url is not set\n", qui);
        return false;
    }
    if (!isConnected()) return false;

    while (backendUrl.endsWith("/")) backendUrl.remove(backendUrl.length() - 1);
    const String url = backendUrl + ruta;

    const bool isHttps = backendUrl.startsWith("https://");
    if (isHttps && !_tlsReady) {
        // Same roots a browser trusts, so Let's Encrypt validates out of the
        // box. "backend_ca" in the config pins a certificate instead.
        const char* ca = (*_config)["backend_ca"] | "";
        if (strlen(ca) > 0) _tls.setCACert(ca);
        else                _tls.setCACertBundle(
                                rootca_crt_bundle_start,
                                rootca_crt_bundle_end - rootca_crt_bundle_start);
        _tlsReady = true;
    }

    HTTPClient& http = _http;
    http.setConnectTimeout(kHttpTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);
    // Keep-alive, so end() below parks the connection instead of closing it and
    // the next request skips the handshake entirely.
    http.setReuse(true);
    if (!(isHttps ? http.begin(_tls, url) : http.begin(_plain, url))) {
        Serial.printf("%s: invalid URL\n", qui);
        return false;
    }

    const char* token = (*_config)["api_token"] | "";
    if (strlen(token) > 0) http.addHeader("X-API-Token", token);

    // /look returns what it decided to say in X-Bonsai-Text (base64 UTF-8) and
    // the format of the audio in X-Bonsai-Rate, which the sink needs before the
    // first sample can go anywhere.
    const char* wanted[] = { "X-Bonsai-Text", "X-Bonsai-Rate" };
    http.collectHeaders(wanted, 2);

    // POST()/GET() return once the response headers are in, so this one number
    // covers connect + TLS handshake + upload + everything the backend did.
    // What it does not cover is the download, which is timed separately below:
    // telling those two apart is the whole point, because one is somebody
    // else's problem and the other is this board's.
    const uint32_t tSent = millis();
    int codi;
    if (body && bodyLen > 0) {
        http.addHeader("Content-Type", "application/json");
        codi = http.POST(const_cast<uint8_t*>(body), bodyLen);
    } else {
        codi = http.GET();
    }
    const uint32_t waitMs = millis() - tSent;

    if (codi != HTTP_CODE_OK) {
        // On a 4xx the body is {"detail": "..."} saying what went wrong.
        Serial.printf("%s: HTTP %d %s\n", qui, codi,
                      http.errorToString(codi).c_str());
        if (codi > 0) Serial.println(http.getString());
        http.end();
        return false;
    }

    if (textHeader) *textHeader = http.header("X-Bonsai-Text");

    // With wav the backend always sends Content-Length.
    const int mida = http.getSize();

    if (outStr) {
        if (mida > kMaxAudioBytes) {
            Serial.printf("%s: %d too many bytes (limit %d)\n", qui, mida, kMaxAudioBytes);
            http.end();
            return false;
        }
        // Better to say so than to die on a failed malloc.
        if (mida > 0 && static_cast<int>(ESP.getMaxAllocHeap()) < mida + 1) {
            Serial.printf("%s: %d contiguous bytes are needed and the largest "
                          "remaining block is %u — use the streaming path for "
                          "anything this size\n",
                          qui, mida + 1, ESP.getMaxAllocHeap());
            http.end();
            return false;
        }

        // getString() memcpys and keeps the length apart, so the 0x00 bytes of
        // the PCM survive. Callers must use length(), not c_str().
        *outStr = http.getString();
        http.end();

        if (mida > 0 && static_cast<int>(outStr->length()) != mida) {
            Serial.printf("%s: %u bytes arrived out of the %d the server announced\n",
                          qui, outStr->length(), mida);
            return false;
        }
        if (outStr->length() < 44 || !outStr->startsWith("RIFF")) {
            Serial.printf("%s: this is not a WAV (%u bytes)\n", qui, outStr->length());
            return false;
        }
        return true;
    }

    WiFiClient*    stream      = http.getStreamPtr();
    const uint32_t tStreamSink = millis();

    if (sink) {
        // Raw pcm16 has no header and, being produced sentence by sentence,
        // usually arrives chunked with no Content-Length at all — so there is
        // nothing to validate up front and nothing to count down to. It ends
        // when the connection ends.
        const uint32_t rate = http.header("X-Bonsai-Rate").toInt();

        // The lock is held for the whole of playback and not because the card
        // is being written: the amplifier holds GPIO8, which is the card's
        // MISO, so the photo writer must be kept off it until the clip is done.
        SdGuard sd(_sdLock);

        if (sink->onStart && !sink->onStart(rate)) {
            http.end();
            return false;
        }

        SinkStream out(sink);
        const int  written = http.writeToStream(&out);
        const size_t total = out.total();
        http.end();

        const uint32_t streamMs = millis() - tStreamSink;
        Serial.printf("  [t] %s: %u ms to first sample, then %u KB (%s) to the "
                      "amplifier over %u ms\n",
                      qui, waitMs, (unsigned)(total / 1024),
                      mida > 0 ? "sized" : "chunked", streamMs);

        if (written < 0) {
            Serial.printf("%s: the transfer failed after %u bytes (%s)\n",
                          qui, total, http.errorToString(written).c_str());
        }
        return total > 0;
    }

    // Straight to the card, a chunk at a time, so nothing big is ever in RAM.
    if (mida > kMaxStreamBytes) {
        Serial.printf("%s: %d bytes is beyond anything expected here\n", qui, mida);
        http.end();
        return false;
    }

    // From here on the card is being written, so nothing else may touch it.
    // Taken here and not earlier on purpose: everything above — the upload and
    // the wait for the backend — leaves the card free for the photo writer.
    SdGuard sd(_sdLock);

    HeapBuf  chunk(kStreamChunk);
    if (!chunk.p) {
        Serial.printf("%s: no heap for a %u byte buffer\n", qui, kStreamChunk);
        http.end();
        return false;
    }
    uint8_t* buf      = chunk.p;
    uint8_t  head[4]  = {0};
    size_t   headLen  = 0;
    size_t   total    = 0;
    uint32_t lastData = millis();

    // Time spent waiting on the socket versus writing to the card, kept apart:
    // if the download is slow, this says which of the two to blame.
    const uint32_t tStream = millis();
    uint32_t sdMs = 0;

    // readBytes() blocks until the buffer is full or the timeout expires, which
    // is the whole trick: the obvious version polls available() and sleeps when
    // it reads zero, and those sleeps dominate. Measured on this board, polling
    // with a 2 ms nap spent 4.3 s of a 4.8 s download doing nothing at all.
    stream->setTimeout(kStallTimeoutMs);

    while (mida < 0 || static_cast<int>(total) < mida) {
        // kStreamChunk and not sizeof(buf): buf is a pointer now, so sizeof
        // would quietly be 4 and this would crawl through the response.
        size_t want = kStreamChunk;
        if (mida > 0 && static_cast<int>(total + want) > mida) want = mida - total;

        const int n = stream->readBytes(buf, want);
        if (n <= 0) {
            // Timed out, or the far end closed. For a chunked response with no
            // Content-Length that is simply the end of it.
            if (mida < 0) break;
            Serial.printf("%s: stopped sending after %u of %d bytes\n",
                          qui, total, mida);
            http.end();
            return false;
        }
        lastData = millis();

        // Keep the first four bytes to check the format once it is all in.
        if (total == 0) {
            for (int i = 0; i < n && headLen < sizeof(head); ++i) head[headLen++] = buf[i];
        }

        const uint32_t tw = millis();
        if (outFile->write(buf, n) != static_cast<size_t>(n)) {
            Serial.printf("%s: the card stopped accepting data at %u bytes\n", qui, total);
            http.end();
            return false;
        }
        sdMs += millis() - tw;
        total += n;
    }
    http.end();
    const uint32_t streamMs = millis() - tStream;

    if (mida > 0 && static_cast<int>(total) != mida) {
        Serial.printf("%s: %u bytes arrived out of the %d the server announced\n",
                      qui, total, mida);
        return false;
    }
    if (total < 44 || memcmp(head, "RIFF", 4) != 0) {
        Serial.printf("%s: this is not a WAV (%u bytes)\n", qui, total);
        return false;
    }

    Serial.printf("  [t] %s: %u ms waiting for the backend, %u ms to download "
                  "%u KB (%u KB/s) of which %u ms was the card\n",
                  qui, waitMs, streamMs, (unsigned)(total / 1024),
                  streamMs ? (unsigned)(total / streamMs) : 0u, sdMs);
    return true;
}

// Shared by textToSpeech() and defaultAudio(): both only differ in the URL.
String WiFiManager::_getAudio(const String& ruta, const char* qui) {
    String wav;
    if (!_request(ruta, qui, nullptr, 0, &wav, nullptr, nullptr, nullptr)) return "";
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
    // No text here: the backend decides what each clip says (audios.py).
    return _getAudio(String("/api/v1/audios/") + urlEncode(clipId)
                     + "?lang=" + bonsai::lang()
                     + "&audioFormat=wav&sampleRate=16000",
                     "defaultAudio");
}

bool WiFiManager::defaultAudioToFile(const String& clipId, File& out) {
    if (clipId.length() == 0) return false;
    return _request(String("/api/v1/audios/") + urlEncode(clipId)
                    + "?lang=" + bonsai::lang()
                    + "&audioFormat=wav&sampleRate=16000",
                    "defaultAudio", nullptr, 0, nullptr, &out, nullptr, nullptr);
}

bool WiFiManager::look(const uint8_t* jpeg, size_t jpegLen, File& out,
                       String* spokenText) {
    // wav, because playWav() needs a RIFF header on the file it is given.
    return _look(jpeg, jpegLen, "wav", &out, nullptr, spokenText);
}

bool WiFiManager::lookStreaming(const uint8_t* jpeg, size_t jpegLen,
                                const AudioSink& sink, String* spokenText) {
    // pcm16: raw signed 16-bit samples, no header to skip and no file to write.
    return _look(jpeg, jpegLen, "pcm16", nullptr, &sink, spokenText);
}

bool WiFiManager::_look(const uint8_t* jpeg, size_t jpegLen,
                        const char* audioFormat, File* out,
                        const AudioSink* sink, String* spokenText) {
    if (!jpeg || jpegLen == 0) return false;
    const uint32_t tPrep = millis();

    // The body is the photo base64'd inside JSON, so it is 4/3 of the frame
    // plus change — hundreds of KB. It goes in PSRAM: internal heap has nothing
    // like that free, which is the same wall the clip downloads hit.
    const size_t b64Cap = ((jpegLen + 2) / 3) * 4 + 1;
    char* b64 = (char*)heap_caps_malloc(b64Cap, MALLOC_CAP_SPIRAM);
    if (!b64) {
        Serial.printf("look: no PSRAM for %u bytes of base64\n", b64Cap);
        return false;
    }

    size_t b64Len = 0;
    if (mbedtls_base64_encode((unsigned char*)b64, b64Cap, &b64Len,
                              jpeg, jpegLen) != 0) {
        Serial.println("look: base64 encoding failed");
        heap_caps_free(b64);
        return false;
    }

    const String deviceId = WiFi.macAddress();
    const String lang     = bonsai::lang();

    // Assembled by hand rather than with ArduinoJson: the document would be a
    // second copy of the base64 and there is no reason to hold two.
    const String head = String("{\"image\":\"");
    // 8 kHz by default when streaming, and that is a measurement rather than a
    // preference: this link runs at roughly 23 KB/s, while 16 kHz 16-bit mono
    // needs 32 KB/s to keep up, so the speaker would keep running dry no matter
    // how the buffering is arranged. 8 kHz needs 16 KB/s and fits with room to
    // spare, at telephone quality — which is what speech is anyway. A file on
    // the card has no such constraint, so that path stays at 16 kHz.
    // Override either with "look_rate" in the config.
    const uint32_t rate = (*_config)["look_rate"] | (sink ? 8000 : 16000);

    const String tail = String("\",\"deviceId\":\"") + deviceId
                      + "\",\"lang\":\"" + lang
                      + "\",\"audioFormat\":\"" + audioFormat
                      + "\",\"sampleRate\":" + String(rate) + "}";

    const size_t bodyLen = head.length() + b64Len + tail.length();
    uint8_t* body = (uint8_t*)heap_caps_malloc(bodyLen, MALLOC_CAP_SPIRAM);
    if (!body) {
        Serial.printf("look: no PSRAM for a %u byte body\n", bodyLen);
        heap_caps_free(b64);
        return false;
    }
    memcpy(body, head.c_str(), head.length());
    memcpy(body + head.length(), b64, b64Len);
    memcpy(body + head.length() + b64Len, tail.c_str(), tail.length());
    heap_caps_free(b64);

    Serial.printf("  [t] look: %u ms to base64 and wrap a %u byte photo "
                  "into %u bytes of JSON\n", millis() - tPrep, jpegLen, bodyLen);

    String text;
    const bool ok = _request("/api/v1/look", "look", body, bodyLen,
                             nullptr, out, sink, &text);
    heap_caps_free(body);

    if (ok && text.length() > 0) {
        // Base64 UTF-8, decoded in place so the description can be logged.
        const size_t cap = (text.length() * 3) / 4 + 2;
        unsigned char* plain = (unsigned char*)malloc(cap);
        if (plain) {
            size_t len = 0;
            if (mbedtls_base64_decode(plain, cap, &len,
                                      (const unsigned char*)text.c_str(),
                                      text.length()) == 0) {
                plain[len] = '\0';
                Serial.printf("look: \"%s\"\n", (const char*)plain);
                if (spokenText) *spokenText = String((const char*)plain);
            }
            free(plain);
        }
    }
    return ok;
}
