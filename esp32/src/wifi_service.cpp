#include <wifi_service.h>
#include <communication/webserver.h>

static const char *TAG = "WiFiService";

WiFiService::WiFiService()
    : _persistence(WiFiSettings_read, WiFiSettings_update, this, WIFI_SETTINGS_FILE,
                   api_WifiSettings_fields, api_WifiSettings_size, WiFiSettings_defaults()),
      protoEndpoint(WiFiSettings_read, WiFiSettings_update, this,
                    API_REQUEST_EXTRACTOR(wifi_settings, api_WifiSettings),
                    API_RESPONSE_ASSIGNER(wifi_settings, api_WifiSettings)) {
    : _persistence(WiFiSettings::read, WiFiSettings::update, this, WIFI_SETTINGS_FILE),
      _lastConnectionAttempt(0),
      _stopping(false),
      endpoint(WiFiSettings::read, WiFiSettings::update, this) {
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
    _lastConnectionAttempt = 0;

    if (state().wifi_networks_count == 1) {
        configureNetwork(state().wifi_networks[0]);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    if (!state().wifiSettings.empty()) {
        WiFi.mode(WIFI_MODE_STA);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        configureNetwork(state().wifiSettings[0]);
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

    mdns_init();
    mdns_hostname_set(state().hostname.c_str());
    mdns_instance_name_set(hostname);
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    mdns_service_add(nullptr, "_ws", "_tcp", 80, nullptr, 0);
    mdns_txt_item_t txtData = {"Firmware Version", APP_VERSION};
    mdns_service_txt_set("_http", "_tcp", &txtData, 1);
}

void WiFiService::getNetworks(JsonObject &root) {
    JsonArray networks = root["networks"].to<JsonArray>();
    int numNetworks = WiFi.scanComplete();
    for (int i = 0; i < numNetworks; i++) {
        JsonObject network = networks.add<JsonObject>();
        network["rssi"] = WiFi.RSSI(i);
        network["ssid"] = WiFi.SSID(i).c_str();
        network["bssid"] = WiFi.BSSIDstr(i).c_str();
        network["channel"] = WiFi.channel(i);
        network["encryption_type"] = (uint8_t)WiFi.encryptionType(i);
    }
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

        root["local_ip"] = (uint32_t)(WiFi.localIP());
        root["mac_address"] = WiFi.macAddress().c_str();
        root["rssi"] = WiFi.RSSI();
        root["ssid"] = WiFi.SSID().c_str();
        root["bssid"] = WiFi.BSSIDstr().c_str();
        root["channel"] = WiFi.channel();
        root["subnet_mask"] = (uint32_t)(WiFi.subnetMask());
        root["gateway_ip"] = (uint32_t)(WiFi.gatewayIP());
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
    if (WiFi.isConnected() || state().wifiSettings.empty()) return;
    connectToWiFi();
}

void WiFiService::connectToWiFi() {
    int scanResult = WiFi.scanNetworks();
    if (scanResult < 0) {
        ESP_LOGE(TAG, "WiFi scan failed.");
    } else if (scanResult == 0) {
        ESP_LOGI(TAG, "No networks found.");
    } else {
        ESP_LOGI(TAG, "%d networks found.", scanResult);

        WiFiNetwork *bestNetwork = nullptr;
        int32_t bestNetworkDb = FACTORY_WIFI_RSSI_THRESHOLD;

        for (int i = 0; i < scanResult; ++i) {
            std::string ssid_scan;
            int32_t rssi_scan;
            uint8_t sec_scan;
            uint8_t *BSSID_scan;
            int32_t chan_scan;

            WiFi.getNetworkInfo(i, ssid_scan, sec_scan, rssi_scan, BSSID_scan, chan_scan);

            for (pb_size_t j = 0; j < state().wifi_networks_count; j++) {
                WiFiNetwork &network = state().wifi_networks[j];
            for (auto &network : state().wifiSettings) {
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
        if (!state().priorityBySignalStrength) {
            for (auto &network : state().wifiSettings) {
                if (network.available == true) {
                    ESP_LOGI(TAG, "Connecting to first available network: %s", network.ssid.c_str());
                    configureNetwork(network);
                    break;
                }
            }
        } else if (state().priorityBySignalStrength && bestNetwork) {
            ESP_LOGI(TAG, "Connecting to strongest network: %s", bestNetwork->ssid.c_str());
            configureNetwork(*bestNetwork);
        } else {
            ESP_LOGI(TAG, "No known networks found.");
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
    WiFi.setHostname(state().hostname.c_str());
    WiFi.begin(network.ssid.c_str(), network.password.c_str());

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
