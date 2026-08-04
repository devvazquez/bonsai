#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <functional>
#include "Audio.h"

#define WIFI_MAX_NETWORKS 5

enum class WiFiStatus {
    Connected,
    Disconnected,
    Reconnecting,
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

private:
    WebServer      _server{80};
    bool           _serverStarted  = false;
    StatusCallback _statusCallback = nullptr;

    uint32_t _lastCheckMs       = 0;
    uint32_t _timeoutPerNetwork = 5000;

    bool _tryAllNetworks();
    bool _trySTA(const char* ssid, const char* password);
    void _startServer();
    void _notify(WiFiStatus status, const String& detail = "");

    bool _loadNetworksFromSD(String ssids[], String passes[], uint32_t& count);
    bool _saveNetworksToSD(const String ssids[], const String passes[], uint32_t count);

    Audio _audio;
};
