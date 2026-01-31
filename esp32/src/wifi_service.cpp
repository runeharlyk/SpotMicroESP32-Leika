#include <wifi_service.h>
#include <communication/webserver.h>

static const char *TAG = "WiFiService";

WiFiService::WiFiService()
    : protoEndpoint(WiFiSettings_read, WiFiSettings_update, this,
                    API_REQUEST_EXTRACTOR(wifi_settings, api_WifiSettings),
                    API_RESPONSE_ASSIGNER(wifi_settings, api_WifiSettings)),
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
    _lastConnectionAttempt = 0;

    if (state().wifi_networks_count >= 1) {
        WiFi.mode(WIFI_MODE_STA);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        configureNetwork(state().wifi_networks[0]);
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
    api_Response response = api_Response_init_zero;
    response.status_code = 202;
    response.which_payload = api_Response_empty_message_tag;
    return WebServer::send(request, 202, response, api_Response_fields);
}

esp_err_t WiFiService::getNetworks(httpd_req_t *request) {
    int numNetworks = WiFi.scanComplete();
    if (numNetworks == -1) {
        api_Response response = api_Response_init_zero;
        response.status_code = 202;
        response.which_payload = api_Response_empty_message_tag;
        return WebServer::send(request, 202, response, api_Response_fields);
    } else if (numNetworks < -1) {
        return handleScan(request);
    }

    size_t count = (numNetworks > 20) ? 20 : static_cast<size_t>(numNetworks);

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
    mdns_init();
    mdns_hostname_set(state().hostname);
    mdns_instance_name_set(hostname);
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    mdns_service_add(nullptr, "_ws", "_tcp", 80, nullptr, 0);
    mdns_txt_item_t txtData = {"Firmware Version", APP_VERSION};
    mdns_service_txt_set("_http", "_tcp", &txtData, 1);
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
    if ((WiFi.getMode() & WIFI_MODE_STA) == 0) {
        WiFi.mode(WIFI_MODE_STA);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
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
                if (ssid_scan == network.ssid) {
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
                for (int i = 0; i < scanResult; ++i) {
                    if (WiFi.SSID(i) == network.ssid) {
                        ESP_LOGI(TAG, "Connecting to first available network: %s", network.ssid);
                        configureNetwork(network);
                        WiFi.scanDelete();
                        return;
                    }
                }
            }
        } else if (bestNetwork) {
            ESP_LOGI(TAG, "Connecting to strongest network: %s", bestNetwork->ssid);
            configureNetwork(*bestNetwork);
        } else {
            ESP_LOGI(TAG, "No known networks found.");
        }

        WiFi.scanDelete();
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
