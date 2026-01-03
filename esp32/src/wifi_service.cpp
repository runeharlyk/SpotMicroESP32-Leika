#include <wifi_service.h>

WiFiService::WiFiService()
    : _persistence(WiFiSettings::read, WiFiSettings::update, this, WIFI_SETTINGS_FILE,
                   socket_message_WifiSettingsData_fields),
      endpoint(WiFiSettings::read, WiFiSettings::update, this, socket_message_WifiSettingsData_fields) {
    addUpdateHandler([&](const std::string& originId) { reconfigureWiFiConnection(); }, false);
}

WiFiService::~WiFiService() {}

void WiFiService::begin() {
    WiFi.mode(WIFI_MODE_STA);

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);

    WiFi.onEvent(std::bind(&WiFiService::onStationModeDisconnected, this, std::placeholders::_1, std::placeholders::_2),
                 WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent(std::bind(&WiFiService::onStationModeStop, this, std::placeholders::_1, std::placeholders::_2),
                 WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_STOP);

    WiFi.onEvent(onStationModeGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);

    _persistence.readFromFS();
    reconfigureWiFiConnection();

    if (state().wifiSettings.size() == 1) {
        configureNetwork(state().wifiSettings[0]);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void WiFiService::reconfigureWiFiConnection() {
    _lastConnectionAttempt = 0;

    if (WiFi.disconnect(true)) _stopping = true;
}

void WiFiService::loop() { EXECUTE_EVERY_N_MS(reconnectDelay, manageSTA()); }

esp_err_t WiFiService::handleScan(HttpRequest& request) {
    if (WiFi.scanComplete() != -1) {
        WiFi.scanDelete();
        WiFi.scanNetworks(true);
    }
    return request.reply(202);
}

esp_err_t WiFiService::getNetworks(HttpRequest& request) {
    int numNetworks = WiFi.scanComplete();
    if (numNetworks == -1)
        return request.reply(202);
    else if (numNetworks < -1)
        return handleScan(request);

    socket_message_NetworkListData proto = socket_message_NetworkListData_init_zero;
    getNetworks(proto);
    return request.replyProto(proto, socket_message_NetworkListData_fields);
}

void WiFiService::setupMDNS(const char* hostname) {
    MDNS.begin(state().hostname.c_str());
    MDNS.setInstanceName(hostname);
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "Firmware Version", APP_VERSION);
}

void WiFiService::getNetworks(socket_message_NetworkListData& proto) {
    int numNetworks = WiFi.scanComplete();
    proto.networks_count = std::min(numNetworks, 12);
    for (int i = 0; i < (int)proto.networks_count; i++) {
        proto.networks[i].rssi = WiFi.RSSI(i);
        strlcpy(proto.networks[i].ssid, WiFi.SSID(i).c_str(), sizeof(proto.networks[i].ssid));
        strlcpy(proto.networks[i].bssid, WiFi.BSSIDstr(i).c_str(), sizeof(proto.networks[i].bssid));
        proto.networks[i].channel = WiFi.channel(i);
        proto.networks[i].encryption_type = (uint8_t)WiFi.encryptionType(i);
    }
}

esp_err_t WiFiService::getNetworkStatus(HttpRequest& request) {
    socket_message_NetworkStatusData proto = socket_message_NetworkStatusData_init_zero;
    getNetworkStatus(proto);
    return request.replyProto(proto, socket_message_NetworkStatusData_fields);
}

void WiFiService::getNetworkStatus(socket_message_NetworkStatusData& proto) {
    wl_status_t status = WiFi.status();
    proto.status = (uint8_t)status;
    if (status == WL_CONNECTED) {
        root["local_ip"] = (uint32_t)(WiFi.localIP());
        root["mac_address"] = WiFi.macAddress();
        root["rssi"] = WiFi.RSSI();
        root["ssid"] = WiFi.SSID();
        root["bssid"] = WiFi.BSSIDstr();
        root["channel"] = WiFi.channel();
        root["subnet_mask"] = (uint32_t)(WiFi.subnetMask());
        root["gateway_ip"] = (uint32_t)(WiFi.gatewayIP());
        IPAddress dnsIP1 = WiFi.dnsIP(0);
        IPAddress dnsIP2 = WiFi.dnsIP(1);
        if (dnsIP1 != IPAddress(0, 0, 0, 0)) {
            root["dns_ip_1"] = (uint32_t)(dnsIP1);
        }
        if (dnsIP2 != IPAddress(0, 0, 0, 0)) {
            root["dns_ip_2"] = (uint32_t)(dnsIP2);
        }
    }
}

void WiFiService::manageSTA() {
    if (WiFi.isConnected() || state().wifiSettings.empty()) return;
    if ((WiFi.getMode() & WIFI_STA) == 0) connectToWiFi();
}

void WiFiService::connectToWiFi() {
    for (auto& network : state().wifiSettings) {
        network.available = false;
    }

    int scanResult = WiFi.scanNetworks();
    if (scanResult == WIFI_SCAN_FAILED) {
        ESP_LOGE("WiFiSettingsService", "WiFi scan failed.");
    } else if (scanResult == 0) {
        ESP_LOGI("WiFiSettingsService", "No networks found.");
    } else {
        ESP_LOGI("WiFiSettingsService", "%d networks found.", scanResult);

        wifi_settings_t* bestNetwork = nullptr;
        int32_t bestNetworkDb = FACTORY_WIFI_RSSI_THRESHOLD;

        for (int i = 0; i < scanResult; ++i) {
            String ssid_scan;
            int32_t rssi_scan;
            uint8_t sec_scan;
            uint8_t* BSSID_scan;
            int32_t chan_scan;

            WiFi.getNetworkInfo(i, ssid_scan, sec_scan, rssi_scan, BSSID_scan, chan_scan);

            for (auto& network : state().wifiSettings) {
                if (ssid_scan == network.ssid.c_str()) {
                    if (rssi_scan >= FACTORY_WIFI_RSSI_THRESHOLD) {
                        network.available = true;
                    }
                    if (rssi_scan > bestNetworkDb) {
                        bestNetworkDb = rssi_scan;
                        bestNetwork = &network;
                    }
                }
            }
        }

        if (!state().priorityBySignalStrength) {
            for (auto& network : state().wifiSettings) {
                if (network.available == true) {
                    ESP_LOGI("WiFiSettingsService", "Connecting to first available network: %s", network.ssid.c_str());
                    configureNetwork(network);
                    break;
                }
            }
        } else if (state().priorityBySignalStrength && bestNetwork) {
            ESP_LOGI("WiFiSettingsService", "Connecting to strongest network: %s", bestNetwork->ssid.c_str());
            configureNetwork(*bestNetwork);
            WiFi.begin(bestNetwork->ssid.c_str(), bestNetwork->password.c_str());
        } else {
            ESP_LOGI("WiFiSettingsService", "No known networks found.");
        }

        WiFi.scanDelete();
    }
}

void WiFiService::configureNetwork(wifi_settings_t& network) {
    if (network.staticIPConfig) {
        WiFi.config(network.localIP, network.gatewayIP, network.subnetMask, network.dnsIP1, network.dnsIP2);
    } else {
        WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
    }
    WiFi.setHostname(state().hostname.c_str());

    WiFi.begin(network.ssid.c_str(), network.password.c_str());

#if CONFIG_IDF_TARGET_ESP32C3
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
}

void WiFiService::onStationModeDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    WiFi.disconnect(true);
    ESP_LOGI("WiFiStatus", "WiFi Disconnected. Reason code=%d", info.wifi_sta_disconnected.reason);
}

void WiFiService::onStationModeStop(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (_stopping) {
        _lastConnectionAttempt = 0;
        _stopping = false;
    }
    ESP_LOGI("WiFiStatus", "WiFi Connected.");
}

void WiFiService::onStationModeGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
    ESP_LOGI("WiFiStatus", "WiFi Got IP. localIP=%s, hostName=%s", WiFi.localIP().toString().c_str(),
             WiFi.getHostname());
}
