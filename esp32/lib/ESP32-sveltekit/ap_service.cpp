#include <ap_service.h>

static const char *TAG = "APService";

APService::APService()
    : endpoint(APSettings::read, APSettings::update, this),
      _fsPersistence(APSettings::read, APSettings::update, this, AP_SETTINGS_FILE),
      _dnsServer(nullptr),
      _lastManaged(0),
      _reconfigureAp(false) {
    addUpdateHandler([&](const String &originId) { reconfigureAP(); }, false);
}

void APService::begin() {}

void APService::reconfigureAP() {
    _lastManaged = millis() - MANAGE_NETWORK_DELAY;
    _reconfigureAp = true;
    _recoveryMode = false;
}

void APService::recoveryMode() {
    _lastManaged = millis() - MANAGE_NETWORK_DELAY;
    _recoveryMode = true;
    _reconfigureAp = true;
}

void APService::loop() {
    EXECUTE_EVERY_N_MS(MANAGE_NETWORK_DELAY, manageAP());
    handleDNS();
}

void APService::manageAP() {
    WiFiMode_t currentWiFiMode = WiFi.getMode();
    if (_state.provisionMode == AP_MODE_ALWAYS ||
        (_state.provisionMode == AP_MODE_DISCONNECTED && WiFi.status() != WL_CONNECTED) || _recoveryMode) {
        if (_reconfigureAp || currentWiFiMode == WIFI_OFF || currentWiFiMode == WIFI_STA) {
            startAP();
        }
    } else if ((currentWiFiMode == WIFI_AP || currentWiFiMode == WIFI_AP_STA) &&
               (_reconfigureAp || !WiFi.softAPgetStationNum())) {
        stopAP();
    }
    _reconfigureAp = false;
}

void APService::startAP() {
    ESP_LOGI(TAG, "Starting software access point at: %s", _state.ssid.c_str());
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.softAPConfig(_state.localIP, _state.gatewayIP, _state.subnetMask);
    WiFi.softAP(_state.ssid.c_str(), _state.password.c_str(), _state.channel, _state.ssidHidden, _state.maxClients);
#if CONFIG_IDF_TARGET_ESP32C3
    WiFi.setTxPower(WIFI_POWER_8_5dBm); // https://www.wemos.cc/en/latest/c3/c3_mini_1_0_0.html#about-wifi
#endif
    if (!_dnsServer) {
        IPAddress apIp = WiFi.softAPIP();
        _dnsServer = new DNSServer;
        _dnsServer->start(DNS_PORT, "*", apIp);
    }
}

void APService::stopAP() {
    if (_dnsServer) {
        ESP_LOGI(TAG, "Stopping captive portal");
        _dnsServer->stop();
        delete _dnsServer;
        _dnsServer = nullptr;
    }
    WiFi.softAPdisconnect(true);
}

void APService::handleDNS() {
    if (_dnsServer) {
        _dnsServer->processNextRequest();
    }
}

APNetworkStatus APService::getAPNetworkStatus() {
    WiFiMode_t currentWiFiMode = WiFi.getMode();
    bool apActive = currentWiFiMode == WIFI_AP || currentWiFiMode == WIFI_AP_STA;
    if (apActive && _state.provisionMode != AP_MODE_ALWAYS && WiFi.status() == WL_CONNECTED) {
        return APNetworkStatus::LINGERING;
    }
    return apActive ? APNetworkStatus::ACTIVE : APNetworkStatus::INACTIVE;
}

esp_err_t APService::getStatus(PsychicRequest *request) {
    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject root = response.getRoot();

    root["status"] = getAPNetworkStatus();
    root["ip_address"] = WiFi.softAPIP().toString();
    root["mac_address"] = WiFi.softAPmacAddress();
    root["station_num"] = WiFi.softAPgetStationNum();

    return response.send();
}