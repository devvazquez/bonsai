#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

#define BLE_DEVICE_NAME   "Bonsai"
#define SERVICE_UUID      "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CONFIG_CHAR_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define WIFI_CHAR_UUID    "d1b9f8e2-4c3a-4e7f-9a2b-5f6e8d0c1a3b"
#define STATUS_CHAR_UUID  "cba1d466-344c-4be3-ab3f-189f80dd7518"

/**
 * @brief Configuration structure for Bonsai device.
 * @param captureIntervalMs The interval in milliseconds between captures.
 * @param frameSize The frame size for the camera (e.g., 8=VGA, 10=XGA, 13=UXGA, 17=QXGA).
 * @param buttonMode The mode for the button (e.g., 0=photo, 1=video).
 */

struct BonsaiConfig {
    uint32_t captureIntervalMs = 60000;
    uint32_t frameSize         = 10; 
    uint32_t buttonMode        = 0;
};

struct WiFiCommand {
    enum class Action { Add, Remove };
    Action action;
    String ssid;
    String password; // empty for Remove
};

class BLEManager {
public:
    using ConfigCallback = void (*)(const BonsaiConfig&);
    using WiFiCallback   = void (*)(const WiFiCommand&);

    bool begin();
    void notify(const char* status);
    void onConfigReceived(ConfigCallback cb) { _configCallback = cb; }
    void onWiFiCommand(WiFiCallback cb)      { _wifiCallback = cb; }
    bool isConnected() const { return _connected; }
    const BonsaiConfig& config() const { return _config; }

private:
    bool           _connected      = false;
    ConfigCallback _configCallback = nullptr;
    WiFiCallback   _wifiCallback   = nullptr;
    BonsaiConfig   _config;
    Preferences    _prefs;

    BLEServer*         _pServer = nullptr;
    BLECharacteristic* _pConfig = nullptr;
    BLECharacteristic* _pWifi   = nullptr;
    BLECharacteristic* _pStatus = nullptr;

    void _loadConfig();
    void _saveConfig();

    class ServerCallbacks : public BLEServerCallbacks {
    public:
        BLEManager* manager;
        void onConnect(BLEServer*)      override { manager->_connected = true; }
        void onDisconnect(BLEServer* s) override {
            manager->_connected = false;
            s->startAdvertising();
        }
    };

    class ConfigCallbacks : public BLECharacteristicCallbacks {
    public:
        BLEManager* manager;
        void onWrite(BLECharacteristic* pChar) override;
    };

    class WiFiCallbacks : public BLECharacteristicCallbacks {
    public:
        BLEManager* manager;
        void onWrite(BLECharacteristic* pChar) override;
    };

    ServerCallbacks _serverCb;
    ConfigCallbacks _configCb;
    WiFiCallbacks   _wifiCb;
};
