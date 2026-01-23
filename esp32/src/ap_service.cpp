#include <ap_service.h>
#include <communication/webserver.h>

static const char *TAG = "APService";

APService::APService()
    : endpoint(APSettings::read, APSettings::update, this),
      protoEndpoint(APSettings::readProto, APSettings::updateProto, this,
                    API_REQUEST_EXTRACTOR(ap_settings, api_APSettings),
                    API_RESPONSE_ASSIGNER(ap_settings, api_APSettings)),
      _persistence(APSettings::read, APSettings::update, this, AP_SETTINGS_FILE) {
    addUpdateHandler([&](const std::string &originId) { reconfigureAP(); }, false);
}

APService::~APService() {}

void APService::begin() { _persistence.readFromFS(); }

esp_err_t APService::getStatus(httpd_req_t *request) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    status(root);
    return WebServer::sendJson(request, 200, doc);
}

void APService::status(JsonObject &root) {
    root["status"] = getAPNetworkStatus();
    root["ip_address"] = (uint32_t)(WiFi.softAPIP());
    root["mac_address"] = WiFi.softAPmacAddress();
    root["station_num"] = WiFi.softAPgetStationNum();
}

esp_err_t APService::getStatusProto(httpd_req_t *request) {
    api_Response res = api_Response_init_zero;
    res.status_code = 200;
    res.which_payload = api_Response_ap_status_tag;
    statusProto(res.payload.ap_status);
    return NativeServer::sendProto(request, 200, res, api_Response_fields);
}

void APService::statusProto(api_APStatus &proto) {
    proto.status = static_cast<api_APNetworkStatus>(getAPNetworkStatus());
    proto.ip_address = static_cast<uint32_t>(WiFi.softAPIP());
    String mac = WiFi.softAPmacAddress();
    strncpy(proto.mac_address, mac.c_str(), sizeof(proto.mac_address) - 1);
    proto.mac_address[sizeof(proto.mac_address) - 1] = '\0';
    proto.station_num = WiFi.softAPgetStationNum();
}

APNetworkStatus APService::getAPNetworkStatus() {
    WiFiMode_t currentWiFiMode = WiFi.getMode();
    bool apActive = currentWiFiMode == WIFI_AP || currentWiFiMode == WIFI_AP_STA;
    if (apActive && state().provisionMode != AP_MODE_ALWAYS && WiFi.status() == WL_CONNECTED) {
        return APNetworkStatus::LINGERING;
    }
    return apActive ? APNetworkStatus::ACTIVE : APNetworkStatus::INACTIVE;
}

void APService::reconfigureAP() {
    _lastManaged = esp_timer_get_time() / 1000 - MANAGE_NETWORK_DELAY;
    _reconfigureAp = true;
    _recoveryMode = false;
}

void APService::recoveryMode() {
    ESP_LOGI(TAG, "Recovery Mode needed");
    _lastManaged = esp_timer_get_time() / 1000 - MANAGE_NETWORK_DELAY;
    _recoveryMode = true;
    _reconfigureAp = true;
}

void APService::loop() {
    EXECUTE_EVERY_N_MS(MANAGE_NETWORK_DELAY, manageAP());
    handleDNS();
}

void APService::manageAP() {
    WiFiMode_t currentWiFiMode = WiFi.getMode();
    if (state().provisionMode == AP_MODE_ALWAYS ||
        (state().provisionMode == AP_MODE_DISCONNECTED && WiFi.status() != WL_CONNECTED) || _recoveryMode) {
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
    ESP_LOGI(TAG, "Starting software access point: %s", state().ssid.c_str());
    WiFi.softAPConfig(state().localIP, state().gatewayIP, state().subnetMask);
    WiFi.softAP(state().ssid.c_str(), state().password.c_str(), state().channel, state().ssidHidden,
                state().maxClients);
#if CONFIG_IDF_TARGET_ESP32C3
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
    if (!_dnsServer) {
        IPAddress apIp = WiFi.softAPIP();
        ESP_LOGI(TAG, "Starting captive portal on %s", apIp.toString().c_str());
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
    ESP_LOGI(TAG, "Stopping AP");
    WiFi.softAPdisconnect(true);
}

void APService::handleDNS() {
    if (_dnsServer) {
        _dnsServer->processNextRequest();
    }
}
