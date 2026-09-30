#include <wifi_service.h>

static const char *TAG = "WiFiService";

WiFiService::WiFiService()
    : protoHandler(WiFiSettings_readForApp, WiFiSettings_updateFromApp, this, api_WifiSettings_fields),
      _persistence(WiFiSettings_read, WiFiSettings_update, this, WIFI_SETTINGS_FILE, api_WifiSettings_fields,
                   api_WifiSettings_size, WiFiSettings_defaults()) {
    addUpdateHandler([&](const std::string &originId) { reconfigureWiFiConnection(); }, false);
}

WiFiService::~WiFiService() {}

void WiFiService::begin() {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);

    WiFi.onEvent([this](int32_t event, void *data) { this->onStationModeDisconnected(event, data); },
                 WIFI_EVENT_STA_DISCONNECTED);
    WiFi.onEvent([this](int32_t event, void *data) { this->onStationModeGotIP(event, data); }, IP_EVENT_STA_GOT_IP_IDF);

    _persistence.readFromFS();
    mergeFactoryNetwork();
    _nextNetwork = state().selected_network;
    if (state().wifi_networks_count >= 1) WiFi.mode(WIFI_MODE_STA);
}

void WiFiService::loop() {
    uint32_t now = esp_timer_get_time() / 1000;
    uint32_t reconfigureAt = _reconfigureAt.load();
    if (reconfigureAt && now >= reconfigureAt && _reconfigureAt.compare_exchange_strong(reconfigureAt, 0)) {
        WiFi.disconnect(false);
        _nextNetwork = state().selected_network;
        _nextAttemptAt = now;
    }
    EXECUTE_EVERY_N_MS(1000, manageSTA());
}

void WiFiService::reconfigureWiFiConnection() {
    // Called while the change is saved, before its reply is sent: disconnecting now would lose the reply.
    _reconfigureAt = esp_timer_get_time() / 1000 + reconfigureDelay;
}

void WiFiService::manageSTA() {
    if (WiFi.isConnected()) return;
    uint32_t now = esp_timer_get_time() / 1000;
    if (now < _nextAttemptAt) return;

    // A save from the app rewrites the networks on the socket's task: connect with one whole copy.
    // Each failed attempt moves on to the next saved network.
    WiFiNetwork network;
    char hostname[sizeof(WiFiSettings::hostname)];
    uint32_t count = 0;
    uint32_t index = 0;
    read([&](const WiFiSettings &settings) {
        count = settings.wifi_networks_count;
        if (count == 0) return;
        index = _nextNetwork % count;
        network = settings.wifi_networks[index];
        memcpy(hostname, settings.hostname, sizeof(hostname));
    });
    if (count == 0) return;

    // The station is off after WiFi.disconnect(true) or while only the access point runs.
    wifi_mode_t mode = WiFi.getMode();
    if (!(mode & WIFI_MODE_STA)) WiFi.mode(static_cast<wifi_mode_t>(mode | WIFI_MODE_STA));

    _connectingTo = index;
    _nextNetwork = index + 1;
    _nextAttemptAt = now + reconnectDelay;
    ESP_LOGI(TAG, "Connecting to %s", network.ssid);
    configureNetwork(network, hostname);
}

void WiFiService::onStationModeDisconnected(int32_t event, void *event_data) {
    wifi_event_sta_disconnected_t *info = static_cast<wifi_event_sta_disconnected_t *>(event_data);
    ESP_LOGI(TAG, "WiFi Disconnected. Reason code=%d", info ? info->reason : 0);
}

void WiFiService::onStationModeGotIP(int32_t event, void *event_data) {
    // After a later drop, the network that worked is tried first.
    _nextNetwork = _connectingTo;
    ESP_LOGI(TAG, "WiFi Got IP. localIP=%s, hostName=%s", WiFi.localIP().toString().c_str(), WiFi.getHostname());
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

void WiFiService::startScan() {
    if (WiFi.scanComplete() != -1) {
        WiFi.scanDelete();
        WiFi.scanNetworks();
    }
}

bool WiFiService::scanResults(api_WifiNetworkList &list) {
    int numNetworks = WiFi.scanComplete();
    if (numNetworks == -1) return false;
    if (numNetworks < -1) {
        startScan();
        return false;
    }

    const std::vector<wifi_ap_record_t> found = WiFi.scanResults();
    size_t count = std::min<size_t>(found.size(), 20);

    // The list points into this storage, and the reply is encoded after this returns.
    static api_WifiNetworkScan networks[20];
    memset(networks, 0, sizeof(networks));

    for (size_t i = 0; i < count; i++) {
        const wifi_ap_record_t &record = found[i];
        networks[i].rssi = record.rssi;
        strncpy(networks[i].ssid, reinterpret_cast<const char *>(record.ssid), sizeof(networks[i].ssid) - 1);
        snprintf(networks[i].bssid, sizeof(networks[i].bssid), "%02X:%02X:%02X:%02X:%02X:%02X", record.bssid[0],
                 record.bssid[1], record.bssid[2], record.bssid[3], record.bssid[4], record.bssid[5]);
        networks[i].channel = record.primary;
        networks[i].encryption_type = static_cast<uint32_t>(WiFiClass::encryptionType(record.authmode));
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

void WiFiService::configureNetwork(const WiFiNetwork &network, const char *hostname) {
    if (network.static_ip_config) {
        WiFi.config(IPAddress(network.local_ip), IPAddress(network.gateway_ip), IPAddress(network.subnet_mask),
                    IPAddress(network.dns_ip_1), IPAddress(network.dns_ip_2));
    } else {
        WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
    }
    WiFi.setHostname(hostname);
    WiFi.begin(network.ssid, network.password);

#if CONFIG_IDF_TARGET_ESP32C3
    WiFi.setTxPower(8);
#endif
}

