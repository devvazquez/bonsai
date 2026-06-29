#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SD.h>
#include <functional>

#define WIFI_MAX_NETWORKS 5
#define WIFI_AP_SSID      "Bonsai-Setup"
#define WIFI_AP_PASSWORD  "bonsai123"

enum class WiFiStatus {
    Connected,
    Disconnected,
    Reconnecting,
    APStarted,
};

class WiFiManager {
public:
    using StatusCallback = void (*)(WiFiStatus, const String& detail);

    // Try all saved networks; falls back to AP if all fail
    void begin(uint32_t timeoutPerNetworkMs = 5000);

    // Called from loop() — monitors connection and handles reconnects
    void loop();

    void onStatusChange(StatusCallback cb) { _statusCallback = cb; }

    bool   isConnected() const;
    String localIP() const;

    // Called by BLE when the app sends credentials
    void addNetwork(const String& ssid, const String& password);
    void removeNetwork(const String& ssid);

    // Stream ElevenLabs TTS audio; calls onChunk for each received PCM block
    void streamTTS(const String& text, const char* apiKey, const char* voiceId,
                   std::function<void(const uint8_t*, size_t)> onChunk);

private:
    WebServer      _server{80};
    bool           _serverStarted  = false;
    bool           _inAPMode       = false;
    StatusCallback _statusCallback = nullptr;

    uint32_t _lastCheckMs       = 0;
    uint32_t _timeoutPerNetwork = 5000;

    bool _tryAllNetworks();
    bool _trySTA(const char* ssid, const char* password);
    void _startAP();
    void _startServer();
    void _notify(WiFiStatus status, const String& detail = "");

    void _registerRoutes();
    void _handleList();
    void _handleFile();
    void _handleNotFound();
};
