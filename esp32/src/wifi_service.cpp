#include <wifi_service.h>
#include <communication/webserver.h>

WiFiService::WiFiService()
    : _persistence(WiFiSettings_read, WiFiSettings_update, this, WIFI_SETTINGS_FILE,
                   api_WifiSettings_fields, api_WifiSettings_size, WiFiSettings_defaults()),
      protoEndpoint(WiFiSettings_read, WiFiSettings_update, this,
                    API_REQUEST_EXTRACTOR(wifi_settings, api_WifiSettings),
                    API_RESPONSE_ASSIGNER(wifi_settings, api_WifiSettings)) {
    addUpdateHandler([&](const std::string &originId) { reconfigureWiFiConnection(); }, false);
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

    if (state().wifi_networks_count == 1) {
        configureNetwork(state().wifi_networks[0]);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void WiFiService::reconfigureWiFiConnection() {
    _lastConnectionAttempt = 0;

    if (WiFi.disconnect(true)) _stopping = true;
}

void WiFiService::loop() { EXECUTE_EVERY_N_MS(reconnectDelay, manageSTA()); }

esp_err_t WiFiService::handleScan(httpd_req_t *request) {
    if (WiFi.scanComplete() != -1) {
        WiFi.scanDelete();
        WiFi.scanNetworks(true);
    }
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_send(request, nullptr, 0);
}

esp_err_t WiFiService::getNetworks(httpd_req_t *request) {
    int numNetworks = WiFi.scanComplete();
    if (numNetworks == -1) {
        httpd_resp_set_status(request, "202 Accepted");
        return httpd_resp_send(request, nullptr, 0);
    } else if (numNetworks < -1) {
        return handleScan(request);
    }

    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    getNetworks(root);
    return WebServer::sendJson(request, 200, doc);
}

void WiFiService::setupMDNS(const char *hostname) {
    MDNS.begin(state().hostname);
    MDNS.setInstanceName(hostname);
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "Firmware Version", APP_VERSION);
}

void WiFiService::getNetworks(JsonObject &root) {
    JsonArray networks = root["networks"].to<JsonArray>();
    int numNetworks = WiFi.scanComplete();
    for (int i = 0; i < numNetworks; i++) {
        JsonObject network = networks.add<JsonObject>();
        network["rssi"] = WiFi.RSSI(i);
        network["ssid"] = WiFi.SSID(i);
        network["bssid"] = WiFi.BSSIDstr(i);
        network["channel"] = WiFi.channel(i);
        network["encryption_type"] = (uint8_t)WiFi.encryptionType(i);
    }
}

esp_err_t WiFiService::getNetworkStatus(httpd_req_t *request) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    getNetworkStatus(root);
    return WebServer::sendJson(request, 200, doc);
}

void WiFiService::getNetworkStatus(JsonObject &root) {
    wl_status_t status = WiFi.status();
    root["status"] = (uint8_t)status;
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
    if (WiFi.isConnected() || state().wifi_networks_count == 0) return;
    if ((WiFi.getMode() & WIFI_STA) == 0) connectToWiFi();
}

void WiFiService::connectToWiFi() {
    int scanResult = WiFi.scanNetworks();
    if (scanResult == WIFI_SCAN_FAILED) {
        ESP_LOGE("WiFiSettingsService", "WiFi scan failed.");
    } else if (scanResult == 0) {
        ESP_LOGI("WiFiSettingsService", "No networks found.");
    } else {
        ESP_LOGI("WiFiSettingsService", "%d networks found.", scanResult);

        WiFiNetwork *bestNetwork = nullptr;
        int32_t bestNetworkDb = FACTORY_WIFI_RSSI_THRESHOLD;

        for (int i = 0; i < scanResult; ++i) {
            String ssid_scan;
            int32_t rssi_scan;
            uint8_t sec_scan;
            uint8_t *BSSID_scan;
            int32_t chan_scan;

            WiFi.getNetworkInfo(i, ssid_scan, sec_scan, rssi_scan, BSSID_scan, chan_scan);

            for (pb_size_t j = 0; j < state().wifi_networks_count; j++) {
                WiFiNetwork &network = state().wifi_networks[j];
                if (ssid_scan == network.ssid) {
                    if (rssi_scan >= FACTORY_WIFI_RSSI_THRESHOLD) {
                        // Network is available
                    }
                    if (rssi_scan > bestNetworkDb) {
                        bestNetworkDb = rssi_scan;
                        bestNetwork = &network;
                    }
                }
            }
        }

        if (!state().priority_rssi) {
            for (pb_size_t j = 0; j < state().wifi_networks_count; j++) {
                WiFiNetwork &network = state().wifi_networks[j];
                // Check if this network was found in scan
                for (int i = 0; i < scanResult; ++i) {
                    if (WiFi.SSID(i) == network.ssid) {
                        ESP_LOGI("WiFiSettingsService", "Connecting to first available network: %s", network.ssid);
                        configureNetwork(network);
                        WiFi.scanDelete();
                        return;
                    }
                }
            }
        } else if (bestNetwork) {
            ESP_LOGI("WiFiSettingsService", "Connecting to strongest network: %s", bestNetwork->ssid);
            configureNetwork(*bestNetwork);
        } else {
            ESP_LOGI("WiFiSettingsService", "No known networks found.");
        }

        WiFi.scanDelete();
    }
}

void WiFiService::configureNetwork(WiFiNetwork &network) {
    if (network.static_ip_config) {
        WiFi.config(IPAddress(network.local_ip), IPAddress(network.gateway_ip),
                    IPAddress(network.subnet_mask), IPAddress(network.dns_ip_1), IPAddress(network.dns_ip_2));
    } else {
        WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
    }
    WiFi.setHostname(state().hostname);

    WiFi.begin(network.ssid, network.password);

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
