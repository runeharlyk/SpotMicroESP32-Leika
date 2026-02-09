#include <ap_service.h>
#include <communication/webserver.h>

static const char *TAG = "APService";

APService::APService() : _dnsServer(nullptr), _lastManaged(0), _reconfigureAp(false), _recoveryMode(false) {}

APService::~APService() {
    if (_dnsServer) {
        delete _dnsServer;
        _dnsServer = nullptr;
    }
}

void APService::begin() {
    _settings = EventBus::instance().peek<APSettings>();
    _settingsHandle = EventBus::instance().subscribe<APSettings>([this](const APSettings &s) {
        _settings = s;
        reconfigureAP();
    });
}

void APService::registerRoutes(WebServer &s) {
    s.on("/api/ap/status", HTTP_GET, [this](httpd_req_t *request) { return getStatusProto(request); });
}

esp_err_t APService::getStatusProto(httpd_req_t *request) {
    api_Response res = api_Response_init_zero;
    res.status_code = 200;
    res.which_payload = api_Response_ap_status_tag;
    statusProto(res.payload.ap_status);
    return WebServer::send(request, 200, res, api_Response_fields);
}

void APService::statusProto(api_APStatus &proto) {
    proto.status = getAPNetworkStatus();
    proto.ip_address = static_cast<uint32_t>(WiFi.softAPIP());
    std::string mac = WiFi.softAPmacAddress();
    strncpy(proto.mac_address, mac.c_str(), sizeof(proto.mac_address) - 1);
    proto.mac_address[sizeof(proto.mac_address) - 1] = '\0';
    proto.station_num = WiFi.softAPgetStationNum();
}

APNetworkStatus APService::getAPNetworkStatus() {
    wifi_mode_t currentWiFiMode = WiFi.getMode();
    bool apActive = currentWiFiMode == WIFI_MODE_AP || currentWiFiMode == WIFI_MODE_APSTA;
    if (apActive && _settings.provision_mode != AP_MODE_ALWAYS && WiFi.status() == WL_CONNECTED) {
        return LINGERING;
    }
    return apActive ? ACTIVE : INACTIVE;
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
    wifi_mode_t currentWiFiMode = WiFi.getMode();
    if (_settings.provision_mode == AP_MODE_ALWAYS ||
        (_settings.provision_mode == AP_MODE_DISCONNECTED && WiFi.status() != WL_CONNECTED) || _recoveryMode) {
        if (_reconfigureAp || currentWiFiMode == WIFI_MODE_NULL || currentWiFiMode == WIFI_MODE_STA) {
            startAP();
        }
    } else if ((currentWiFiMode == WIFI_MODE_AP || currentWiFiMode == WIFI_MODE_APSTA) &&
               (_reconfigureAp || !WiFi.softAPgetStationNum())) {
        stopAP();
    }
    _reconfigureAp = false;
}

void APService::startAP() {
    ESP_LOGI(TAG, "Starting software access point: %s", _settings.ssid);
    WiFi.softAPConfig(IPAddress(_settings.local_ip), IPAddress(_settings.gateway_ip), IPAddress(_settings.subnet_mask));
    WiFi.softAP(_settings.ssid, _settings.password, _settings.channel, _settings.ssid_hidden, _settings.max_clients);
#if CONFIG_IDF_TARGET_ESP32C3
    WiFi.setTxPower(8);
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

void APService::handleDNS() {}
