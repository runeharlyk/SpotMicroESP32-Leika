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
    // Return 202 with empty_message payload (no pointer fields to encode)
    api_Response response = api_Response_init_zero;
    response.status_code = 202;
    response.which_payload = api_Response_empty_message_tag;
    return WebServer::send(request, 202, response, api_Response_fields);
}

esp_err_t WiFiService::getNetworks(httpd_req_t *request) {
    int numNetworks = WiFi.scanComplete();
    if (numNetworks == -1) {
        // Scan in progress - return 202 with empty_message payload
        api_Response response = api_Response_init_zero;
        response.status_code = 202;
        response.which_payload = api_Response_empty_message_tag;
        return WebServer::send(request, 202, response, api_Response_fields);
    } else if (numNetworks < -1) {
        return handleScan(request);
    }

    // Limit to 20 networks max
    size_t count = (numNetworks > 20) ? 20 : static_cast<size_t>(numNetworks);

    // Allocate networks array on stack (pointer type in proto)
    api_WifiNetworkScan networks[20] = {};

    for (size_t i = 0; i < count; i++) {
        networks[i].rssi = WiFi.RSSI(i);
        strncpy(networks[i].ssid, WiFi.SSID(i).c_str(), sizeof(networks[i].ssid) - 1);
        strncpy(networks[i].bssid, WiFi.BSSIDstr(i).c_str(), sizeof(networks[i].bssid) - 1);
        networks[i].channel = WiFi.channel(i);
        networks[i].encryption_type = static_cast<uint32_t>(WiFi.encryptionType(i));
    }

    api_Response response = api_Response_init_zero;
    response.status_code = 200;
    response.which_payload = api_Response_wifi_network_list_tag;
    response.payload.wifi_network_list.networks = networks;
    response.payload.wifi_network_list.networks_count = count;

    return WebServer::send(request, 200, response, api_Response_fields);
}

void WiFiService::setupMDNS(const char *hostname) {
    MDNS.begin(state().hostname);
    MDNS.setInstanceName(hostname);
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "Firmware Version", APP_VERSION);
}

esp_err_t WiFiService::getNetworkStatus(httpd_req_t *request) {
    api_Response response = api_Response_init_zero;
    response.which_payload = api_Response_wifi_status_tag;
    api_WifiStatus &wifiStatus = response.payload.wifi_status;

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

    return WebServer::send(request, 200, response, api_Response_fields);
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
