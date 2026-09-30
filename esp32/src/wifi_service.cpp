#include <wifi_service.h>

static const char *TAG = "WiFiService";

WiFiService::WiFiService()
    : protoHandler(WiFiSettings_read, WiFiSettings_updateFromApp, this),
      _persistence(WiFiSettings_read, WiFiSettings_update, this, WIFI_SETTINGS_FILE, api_WifiSettings_fields,
                   api_WifiSettings_size, WiFiSettings_defaults()),
      _lastConnectionAttempt(0),
      _stopping(false) {
    addUpdateHandler([&](const std::string &originId) { reconfigureWiFiConnection(); }, false);
}

WiFiService::~WiFiService() {}

void WiFiService::begin() {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);

    WiFi.onEvent([this](int32_t event, void *data) { this->onStationModeDisconnected(event, data); },
                 WIFI_EVENT_STA_DISCONNECTED);
    WiFi.onEvent([this](int32_t event, void *data) { this->onStationModeStop(event, data); }, WIFI_EVENT_STA_STOP);
    WiFi.onEvent(onStationModeGotIP, IP_EVENT_STA_GOT_IP_IDF);

    _persistence.readFromFS();
    mergeFactoryNetwork();
    _lastConnectionAttempt = 0;

    if (state().wifi_networks_count >= 1) {
        WiFi.mode(WIFI_MODE_STA);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        uint32_t idx = state().selected_network;
        if (idx >= state().wifi_networks_count) idx = 0;
        configureNetwork(state().wifi_networks[idx]);
    }
}

void WiFiService::mergeFactoryNetwork() {
    bool changed = false;
    updateWithoutPropagation([&](WiFiSettings &settings) {
        changed = applyFactoryNetwork(settings, SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD);
        return changed ? StateUpdateResult::CHANGED : StateUpdateResult::UNCHANGED;
    });
    if (!changed) return;
    ESP_LOGI(TAG, "Merged the network from secrets.h: %s", SECRET_WIFI_SSID);
    _persistence.writeToFS();
}

void WiFiService::reconfigureWiFiConnection() {
    _lastConnectionAttempt = 0;
    if (WiFi.disconnect(true)) _stopping = true;
}

void WiFiService::selectNetwork(uint32_t index) {
    if (index >= state().wifi_networks_count) return;
    updateWithoutPropagation([&](WiFiSettings &settings) {
        settings.selected_network = index;
        return StateUpdateResult::CHANGED;
    });
    _persistence.writeToFS();
    reconfigureWiFiConnection();
}

void WiFiService::loop() { EXECUTE_EVERY_N_MS(reconnectDelay, manageSTA()); }

void WiFiService::startScan() {
    if (WiFi.scanComplete() != -1) {
        WiFi.scanDelete();
        WiFi.scanNetworks(true);
    }
}

bool WiFiService::scanResults(api_WifiNetworkList &list) {
    int numNetworks = WiFi.scanComplete();
    if (numNetworks == -1) return false;
    if (numNetworks < -1) {
        startScan();
        return false;
    }

    size_t count = (numNetworks > 20) ? 20 : static_cast<size_t>(numNetworks);

    // The list points into this storage, and the reply is encoded after this returns.
    static api_WifiNetworkScan networks[20];
    memset(networks, 0, sizeof(networks));

    for (size_t i = 0; i < count; i++) {
        networks[i].rssi = WiFi.RSSI(i);
        strncpy(networks[i].ssid, WiFi.SSID(i).c_str(), sizeof(networks[i].ssid) - 1);
        strncpy(networks[i].bssid, WiFi.BSSIDstr(i).c_str(), sizeof(networks[i].bssid) - 1);
        networks[i].channel = WiFi.channel(i);
        networks[i].encryption_type = static_cast<uint32_t>(WiFi.encryptionType(i));
    }

    list.networks = networks;
    list.networks_count = count;
    return true;
}

void WiFiService::status(api_WifiStatus &wifiStatus) {
    wl_status_t status = WiFi.status();
    wifiStatus.status = static_cast<uint32_t>(status);

    if (status == WL_CONNECTED) {
        wifiStatus.local_ip = static_cast<uint32_t>(WiFi.localIP());
        strncpy(wifiStatus.mac_address, WiFi.macAddress().c_str(), sizeof(wifiStatus.mac_address) - 1);
        wifiStatus.rssi = WiFi.RSSI();
        strncpy(wifiStatus.ssid, WiFi.SSID().c_str(), sizeof(wifiStatus.ssid) - 1);
        strncpy(wifiStatus.bssid, WiFi.BSSIDstr().c_str(), sizeof(wifiStatus.bssid) - 1);
        wifiStatus.channel = WiFi.channel();
        wifiStatus.subnet_mask = static_cast<uint32_t>(WiFi.subnetMask());
        wifiStatus.gateway_ip = static_cast<uint32_t>(WiFi.gatewayIP());
        IPAddress dnsIP1 = WiFi.dnsIP(0);
        IPAddress dnsIP2 = WiFi.dnsIP(1);
        if (dnsIP1 != IPAddress(0, 0, 0, 0)) {
            wifiStatus.dns_ip_1 = static_cast<uint32_t>(dnsIP1);
        }
        if (dnsIP2 != IPAddress(0, 0, 0, 0)) {
            wifiStatus.dns_ip_2 = static_cast<uint32_t>(dnsIP2);
        }
    }
}

void WiFiService::manageSTA() {
    if (WiFi.isConnected() || state().wifi_networks_count == 0) return;
    wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_NULL || mode == WIFI_MODE_AP) return;

    static uint32_t startTime = 0;
    static bool attempted = false;

    if (startTime == 0) {
        startTime = esp_timer_get_time() / 1000;
        return;
    }

    uint32_t now = esp_timer_get_time() / 1000;
    if (now - startTime < 3000) return;

    if (!attempted && state().wifi_networks_count > 0) {
        attempted = true;
        uint32_t idx = state().selected_network;
        if (idx >= state().wifi_networks_count) idx = 0;
        ESP_LOGI(TAG, "Connecting to: %s", state().wifi_networks[idx].ssid);
        configureNetwork(state().wifi_networks[idx]);
    }
}

void WiFiService::configureNetwork(WiFiNetwork &network) {
    if (network.static_ip_config) {
        WiFi.config(IPAddress(network.local_ip), IPAddress(network.gateway_ip), IPAddress(network.subnet_mask),
                    IPAddress(network.dns_ip_1), IPAddress(network.dns_ip_2));
    } else {
        WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
    }
    WiFi.setHostname(state().hostname);
    WiFi.begin(network.ssid, network.password);

#if CONFIG_IDF_TARGET_ESP32C3
    WiFi.setTxPower(8);
#endif
}

void WiFiService::onStationModeDisconnected(int32_t event, void *event_data) {
    WiFi.disconnect(true);
    wifi_event_sta_disconnected_t *info = static_cast<wifi_event_sta_disconnected_t *>(event_data);
    ESP_LOGI(TAG, "WiFi Disconnected. Reason code=%d", info ? info->reason : 0);
}

void WiFiService::onStationModeStop(int32_t event, void *event_data) {
    if (_stopping) {
        _lastConnectionAttempt = 0;
        _stopping = false;
    }
    ESP_LOGI(TAG, "WiFi STA stopped.");
}

void WiFiService::onStationModeGotIP(int32_t event, void *event_data) {
    ESP_LOGI(TAG, "WiFi Got IP. localIP=%s, hostName=%s", WiFi.localIP().toString().c_str(), WiFi.getHostname());
}
