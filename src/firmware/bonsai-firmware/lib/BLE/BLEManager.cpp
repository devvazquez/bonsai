#include "BLEManager.h"

struct ConfigField {
    const char* key;
    size_t      offset;
    uint32_t    defaultVal;
};

static const ConfigField CONFIG_FIELDS[] = {
    { "captureIntervalMs", offsetof(BonsaiConfig, captureIntervalMs), 60000 },
    { "frameSize",         offsetof(BonsaiConfig, frameSize),         10    },
};

static constexpr size_t NUM_FIELDS = sizeof(CONFIG_FIELDS) / sizeof(CONFIG_FIELDS[0]);

static uint32_t& fieldRef(BonsaiConfig& cfg, size_t offset) {
    return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&cfg) + offset);
}

// ---------------------------------------------------------------

void BLEManager::_loadConfig() {
    _prefs.begin("bonsai", true);
    for (size_t i = 0; i < NUM_FIELDS; i++) {
        fieldRef(_config, CONFIG_FIELDS[i].offset) =
            _prefs.getUInt(CONFIG_FIELDS[i].key, CONFIG_FIELDS[i].defaultVal);
    }
    _prefs.end();
}

void BLEManager::_saveConfig() {
    _prefs.begin("bonsai", false);
    for (size_t i = 0; i < NUM_FIELDS; i++) {
        _prefs.putUInt(CONFIG_FIELDS[i].key, fieldRef(_config, CONFIG_FIELDS[i].offset));
    }
    _prefs.end();
}

bool BLEManager::begin() {
    _loadConfig();

    BLEDevice::init(BLE_DEVICE_NAME);

    _pServer = BLEDevice::createServer();
    _serverCb.manager = this;
    _pServer->setCallbacks(&_serverCb);

    BLEService* pService = _pServer->createService(SERVICE_UUID);

    // Status: server → app (notify)
    _pStatus = pService->createCharacteristic(
        STATUS_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    _pStatus->addDescriptor(new BLE2902());
    _pStatus->setValue("idle");

    // Config: app → server (device settings)
    _pConfig = pService->createCharacteristic(
        CONFIG_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ
    );
    _configCb.manager = this;
    _pConfig->setCallbacks(&_configCb);

    // WiFi: app → server (add/remove networks)
    // Format: "add=MySSID,mypassword" or "remove=MySSID"
    _pWifi = pService->createCharacteristic(
        WIFI_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE
    );
    _wifiCb.manager = this;
    _pWifi->setCallbacks(&_wifiCb);

    pService->start();

    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();

    return true;
}

void BLEManager::notify(const char* status) {
    if (!_connected) return;
    _pStatus->setValue(status);
    _pStatus->notify();
}

void BLEManager::ConfigCallbacks::onWrite(BLECharacteristic* pChar) {
    String raw = pChar->getValue().c_str();
    if (raw.length() == 0) return;

    int eqIdx = raw.indexOf('=');
    if (eqIdx == -1) return;

    String key   = raw.substring(0, eqIdx);
    String value = raw.substring(eqIdx + 1);
    key.trim();
    value.trim();

    for (size_t i = 0; i < NUM_FIELDS; i++) {
        if (key == CONFIG_FIELDS[i].key) {
            fieldRef(manager->_config, CONFIG_FIELDS[i].offset) = (uint32_t)value.toInt();
            break;
        }
    }

    manager->_saveConfig();

    if (manager->_configCallback) {
        manager->_configCallback(manager->_config);
    }
}

void BLEManager::WiFiCallbacks::onWrite(BLECharacteristic* pChar) {
    String raw = pChar->getValue().c_str();
    if (raw.length() == 0) return;

    int eqIdx = raw.indexOf('=');
    if (eqIdx == -1) return;

    String action = raw.substring(0, eqIdx);
    String rest   = raw.substring(eqIdx + 1);
    action.trim();

    WiFiCommand cmd;

    if (action == "add") {
        int commaIdx = rest.indexOf(',');
        if (commaIdx == -1) return;
        cmd.action   = WiFiCommand::Action::Add;
        cmd.ssid     = rest.substring(0, commaIdx);
        cmd.password = rest.substring(commaIdx + 1);
        cmd.ssid.trim();
        cmd.password.trim();
    } else if (action == "remove") {
        cmd.action = WiFiCommand::Action::Remove;
        cmd.ssid   = rest;
        cmd.ssid.trim();
    } else {
        return;
    }

    if (manager->_wifiCallback) {
        manager->_wifiCallback(cmd);
    }
}
