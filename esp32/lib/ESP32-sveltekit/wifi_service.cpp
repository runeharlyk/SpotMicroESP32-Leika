#include <wifi_service.h>

WiFiService::WiFiService()
    : endpoint(WiFiSettings::read, WiFiSettings::update, this),
      _persistence(WiFiSettings::read, WiFiSettings::update, this, WIFI_SETTINGS_FILE) {
    addUpdateHandler([&](const String &originId) { reconfigureWiFiConnection(); }, false);
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
        delay(500);
    }
}

void WiFiService::reconfigureWiFiConnection() {
    _lastConnectionAttempt = 0;

    if (WiFi.disconnect(true)) _stopping = true;
}

void WiFiService::loop() { EXECUTE_EVERY_N_MS(reconnectDelay, manageSTA()); }

esp_err_t WiFiService::handleScan(PsychicRequest *request) {
    if (WiFi.scanComplete() != -1) {
        WiFi.scanDelete();
        WiFi.scanNetworks(true);
    }
    return request->reply(202);
}

esp_err_t WiFiService::getNetworks(PsychicRequest *request) {
    int numNetworks = WiFi.scanComplete();
    if (numNetworks == -1)
        return request->reply(202);
    else if (numNetworks < -1)
        return handleScan(request);

    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject root = response.getRoot();
    getNetworks(root);
    return response.send();
}

void WiFiService::setupMDNS(const char *hostname) {
    MDNS.begin(state().hostname.c_str());
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

esp_err_t WiFiService::getNetworkStatus(PsychicRequest *request) {
    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject root = response.getRoot();
    getNetworkStatus(root);
    return response.send();
}

void WiFiService::getNetworkStatus(JsonObject &root) {
    wl_status_t status = WiFi.status();
    root["status"] = (uint8_t)status;
    if (status == WL_CONNECTED) {
        root["local_ip"] = WiFi.localIP().toString();
        root["mac_address"] = WiFi.macAddress();
        root["rssi"] = WiFi.RSSI();
        root["ssid"] = WiFi.SSID();
        root["bssid"] = WiFi.BSSIDstr();
        root["channel"] = WiFi.channel();
        root["subnet_mask"] = WiFi.subnetMask().toString();
        root["gateway_ip"] = WiFi.gatewayIP().toString();
        IPAddress dnsIP1 = WiFi.dnsIP(0);
        IPAddress dnsIP2 = WiFi.dnsIP(1);
        if (dnsIP1 != INADDR_NONE) {
            root["dns_ip_1"] = dnsIP1.toString();
        }
        if (dnsIP2 != INADDR_NONE) {
            root["dns_ip_2"] = dnsIP2.toString();
        }
    }
}

void WiFiService::manageSTA() {
    if (WiFi.isConnected() || state().wifiSettings.empty()) return;
    if ((WiFi.getMode() & WIFI_STA) == 0) connectToWiFi();
}

void WiFiService::connectToWiFi() {
    // reset availability flag for all stored networks
    for (auto &network : state().wifiSettings) {
        network.available = false;
    }

    // scanning for available networks
    int scanResult = WiFi.scanNetworks();
    if (scanResult == WIFI_SCAN_FAILED) {
        ESP_LOGE("WiFiSettingsService", "WiFi scan failed.");
    } else if (scanResult == 0) {
        ESP_LOGI("WiFiSettingsService", "No networks found.");
    } else {
        ESP_LOGI("WiFiSettingsService", "%d networks found.", scanResult);

        // find the best network to connect
        wifi_settings_t *bestNetwork = nullptr;
        int32_t bestNetworkDb = FACTORY_WIFI_RSSI_THRESHOLD;

        for (int i = 0; i < scanResult; ++i) {
            String ssid_scan;
            int32_t rssi_scan;
            uint8_t sec_scan;
            uint8_t *BSSID_scan;
            int32_t chan_scan;

            WiFi.getNetworkInfo(i, ssid_scan, sec_scan, rssi_scan, BSSID_scan, chan_scan);

            for (auto &network : state().wifiSettings) {
                if (ssid_scan == network.ssid) {
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
            for (auto &network : state().wifiSettings) {
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

void WiFiService::configureNetwork(wifi_settings_t &network) {
    if (network.staticIPConfig) {
        // configure for static IP
        WiFi.config(network.localIP, network.gatewayIP, network.subnetMask, network.dnsIP1, network.dnsIP2);
    } else {
        // configure for DHCP
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }
    WiFi.setHostname(state().hostname.c_str());

    // attempt to connect to the network
    WiFi.begin(network.ssid.c_str(), network.password.c_str());

#if CONFIG_IDF_TARGET_ESP32C3
    WiFi.setTxPower(WIFI_POWER_8_5dBm); // https://www.wemos.cc/en/latest/c3/c3_mini_1_0_0.html#about-wifi
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