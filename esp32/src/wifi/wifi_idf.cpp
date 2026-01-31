#include <wifi/wifi_idf.h>

static const char* TAG = "WiFiIDF";

WiFiClass WiFi;

WiFiClass::WiFiClass()
    : _sta_netif(nullptr),
      _ap_netif(nullptr),
      _initialized(false),
      _autoReconnect(false),
      _persistent(false),
      _status(WL_DISCONNECTED),
      _mode(WIFI_MODE_NULL),
      _scanResult(nullptr),
      _scanCount(0),
      _scanStatus(-2),
      _useStaticIp(false) {}

WiFiClass::~WiFiClass() {
    if (_scanResult) {
        free(_scanResult);
        _scanResult = nullptr;
    }
}

bool WiFiClass::init() {
    if (_initialized) return true;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event loop create failed: %s", esp_err_to_name(ret));
    }

    _sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!_sta_netif) {
        _sta_netif = esp_netif_create_default_wifi_sta();
    }

    _ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!_ap_netif) {
        _ap_netif = esp_netif_create_default_wifi_ap();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return false;
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiClass::eventHandler, this, nullptr);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &WiFiClass::eventHandler, this, nullptr);

    esp_wifi_set_storage(_persistent ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM);

    _initialized = true;
    return true;
}

void WiFiClass::eventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    WiFiClass* self = static_cast<WiFiClass*>(arg);

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START: ESP_LOGI(TAG, "STA Started"); break;
            case WIFI_EVENT_STA_STOP:
                ESP_LOGI(TAG, "STA Stopped");
                self->_status = WL_DISCONNECTED;
                break;
            case WIFI_EVENT_STA_CONNECTED: ESP_LOGI(TAG, "STA Connected"); break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*)event_data;
                ESP_LOGI(TAG, "STA Disconnected, reason: %d", event->reason);
                self->_status = WL_DISCONNECTED;
                if (self->_autoReconnect) {
                    esp_wifi_connect();
                }
                break;
            }
            case WIFI_EVENT_AP_START: ESP_LOGI(TAG, "AP Started"); break;
            case WIFI_EVENT_AP_STOP: ESP_LOGI(TAG, "AP Stopped"); break;
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
                ESP_LOGI(TAG, "Station connected, AID=%d", event->aid);
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
                ESP_LOGI(TAG, "Station disconnected, AID=%d", event->aid);
                break;
            }
            case WIFI_EVENT_SCAN_DONE: {
                wifi_event_sta_scan_done_t* event = (wifi_event_sta_scan_done_t*)event_data;
                if (event->status == 0) {
                    self->_scanCount = event->number;
                    if (self->_scanResult) {
                        free(self->_scanResult);
                    }
                    self->_scanResult = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * self->_scanCount);
                    if (self->_scanResult) {
                        esp_wifi_scan_get_ap_records(&self->_scanCount, self->_scanResult);
                    }
                    self->_scanStatus = self->_scanCount;
                } else {
                    self->_scanStatus = -2;
                }
                break;
            }
        }
        self->dispatchEvent(event_id, event_data);
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            self->_status = WL_CONNECTED;
            self->dispatchEvent(event_id + 1000, event_data);
        } else if (event_id == IP_EVENT_STA_LOST_IP) {
            ESP_LOGI(TAG, "Lost IP");
            self->_status = WL_DISCONNECTED;
            self->dispatchEvent(event_id + 1000, event_data);
        }
    }
}

void WiFiClass::dispatchEvent(int32_t event_id, void* event_data) {
    for (auto& handler : _eventHandlers) {
        if (handler.event_id == event_id || handler.event_id == -1) {
            handler.callback(event_id, event_data);
        }
    }
}

void WiFiClass::onEvent(WiFiEventCb callback, int32_t event_id) { _eventHandlers.push_back({event_id, callback}); }

bool WiFiClass::mode(wifi_mode_t m) {
    if (!_initialized) init();

    if (_mode == m) return true;

    esp_err_t err;
    if (_mode == WIFI_MODE_NULL) {
        err = esp_wifi_set_mode(m);
        if (err == ESP_OK) {
            err = esp_wifi_start();
        }
    } else if (m == WIFI_MODE_NULL) {
        err = esp_wifi_stop();
    } else {
        err = esp_wifi_set_mode(m);
    }

    if (err == ESP_OK) {
        _mode = m;
        return true;
    }
    ESP_LOGE(TAG, "Failed to set mode: %s", esp_err_to_name(err));
    return false;
}

wifi_mode_t WiFiClass::getMode() {
    if (!_initialized) return WIFI_MODE_NULL;
    wifi_mode_t m;
    if (esp_wifi_get_mode(&m) == ESP_OK) {
        return m;
    }
    return WIFI_MODE_NULL;
}

bool WiFiClass::begin(const char* ssid, const char* password, int32_t channel, const uint8_t* bssid) {
    if (!_initialized) init();

    wifi_mode_t currentMode = getMode();
    if (currentMode == WIFI_MODE_NULL || currentMode == WIFI_MODE_AP) {
        mode(currentMode == WIFI_MODE_AP ? WIFI_MODE_APSTA : WIFI_MODE_STA);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    if (channel > 0) {
        wifi_config.sta.channel = channel;
    }
    if (bssid) {
        memcpy(wifi_config.sta.bssid, bssid, 6);
        wifi_config.sta.bssid_set = true;
    }

    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.authmode = password && strlen(password) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA config: %s", esp_err_to_name(err));
        return false;
    }

    if (_useStaticIp) {
        esp_netif_dhcpc_stop(_sta_netif);
        esp_netif_ip_info_t ip_info = {};
        ip_info.ip = _sta_static_ip;
        ip_info.gw = _sta_static_gw;
        ip_info.netmask = _sta_static_sn;
        esp_netif_set_ip_info(_sta_netif, &ip_info);

        if (static_cast<uint32_t>(_sta_static_dns1) != 0) {
            esp_netif_dns_info_t dns;
            dns.ip.u_addr.ip4 = _sta_static_dns1;
            dns.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_set_dns_info(_sta_netif, ESP_NETIF_DNS_MAIN, &dns);
        }
        if (static_cast<uint32_t>(_sta_static_dns2) != 0) {
            esp_netif_dns_info_t dns;
            dns.ip.u_addr.ip4 = _sta_static_dns2;
            dns.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_set_dns_info(_sta_netif, ESP_NETIF_DNS_BACKUP, &dns);
        }
    } else {
        esp_netif_dhcpc_start(_sta_netif);
    }

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

bool WiFiClass::disconnect(bool wifiOff) {
    esp_err_t err = esp_wifi_disconnect();
    if (wifiOff) {
        wifi_mode_t m = getMode();
        if (m == WIFI_MODE_APSTA) {
            mode(WIFI_MODE_AP);
        } else if (m == WIFI_MODE_STA) {
            mode(WIFI_MODE_NULL);
        }
    }
    _status = WL_DISCONNECTED;
    return err == ESP_OK;
}

bool WiFiClass::reconnect() { return esp_wifi_connect() == ESP_OK; }

bool WiFiClass::config(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dns1, IPAddress dns2) {
    _sta_static_ip = local_ip;
    _sta_static_gw = gateway;
    _sta_static_sn = subnet;
    _sta_static_dns1 = dns1;
    _sta_static_dns2 = dns2;
    _useStaticIp = (static_cast<uint32_t>(local_ip) != 0);
    return true;
}

bool WiFiClass::setHostname(const char* hostname) {
    _hostname = hostname;
    if (_sta_netif) {
        return esp_netif_set_hostname(_sta_netif, hostname) == ESP_OK;
    }
    return true;
}

const char* WiFiClass::getHostname() {
    const char* hostname = nullptr;
    if (_sta_netif) {
        esp_netif_get_hostname(_sta_netif, &hostname);
    }
    return hostname ? hostname : _hostname.c_str();
}

wl_status_t WiFiClass::status() { return _status; }

bool WiFiClass::isConnected() { return _status == WL_CONNECTED; }

IPAddress WiFiClass::localIP() {
    esp_netif_ip_info_t ip_info;
    if (_sta_netif && esp_netif_get_ip_info(_sta_netif, &ip_info) == ESP_OK) {
        return IPAddress(ip_info.ip);
    }
    return IPAddress();
}

IPAddress WiFiClass::subnetMask() {
    esp_netif_ip_info_t ip_info;
    if (_sta_netif && esp_netif_get_ip_info(_sta_netif, &ip_info) == ESP_OK) {
        return IPAddress(ip_info.netmask);
    }
    return IPAddress();
}

IPAddress WiFiClass::gatewayIP() {
    esp_netif_ip_info_t ip_info;
    if (_sta_netif && esp_netif_get_ip_info(_sta_netif, &ip_info) == ESP_OK) {
        return IPAddress(ip_info.gw);
    }
    return IPAddress();
}

IPAddress WiFiClass::dnsIP(uint8_t dns_no) {
    esp_netif_dns_info_t dns;
    esp_netif_dns_type_t type = dns_no == 0 ? ESP_NETIF_DNS_MAIN : ESP_NETIF_DNS_BACKUP;
    if (_sta_netif && esp_netif_get_dns_info(_sta_netif, type, &dns) == ESP_OK) {
        return IPAddress(dns.ip.u_addr.ip4);
    }
    return IPAddress();
}

std::string WiFiClass::macAddress() {
    uint8_t mac[6];
    char buf[18];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buf);
}

std::string WiFiClass::SSID() {
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
        return std::string((char*)info.ssid);
    }
    return "";
}

std::string WiFiClass::BSSIDstr() {
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", info.bssid[0], info.bssid[1], info.bssid[2],
                 info.bssid[3], info.bssid[4], info.bssid[5]);
        return std::string(buf);
    }
    return "";
}

int32_t WiFiClass::RSSI() {
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
        return info.rssi;
    }
    return 0;
}

uint8_t WiFiClass::channel() {
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
        return info.primary;
    }
    return 0;
}

int16_t WiFiClass::scanNetworks(bool async) {
    if (!_initialized) init();

    wifi_mode_t currentMode = getMode();
    if (currentMode == WIFI_MODE_NULL || currentMode == WIFI_MODE_AP) {
        mode(currentMode == WIFI_MODE_AP ? WIFI_MODE_APSTA : WIFI_MODE_STA);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    _scanStatus = -1;

    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = true;

    esp_err_t err = esp_wifi_scan_start(&scan_config, !async);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
        _scanStatus = -2;
        return -2;
    }

    if (!async) {
        if (_scanResult) {
            free(_scanResult);
            _scanResult = nullptr;
        }
        esp_wifi_scan_get_ap_num(&_scanCount);
        _scanResult = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * _scanCount);
        if (_scanResult) {
            esp_wifi_scan_get_ap_records(&_scanCount, _scanResult);
        }
        _scanStatus = _scanCount;
        return _scanCount;
    }

    return -1;
}

int16_t WiFiClass::scanComplete() { return _scanStatus; }

void WiFiClass::scanDelete() {
    if (_scanResult) {
        free(_scanResult);
        _scanResult = nullptr;
    }
    _scanCount = 0;
    _scanStatus = -2;
}

std::string WiFiClass::SSID(uint8_t i) {
    if (i < _scanCount && _scanResult) {
        return std::string((char*)_scanResult[i].ssid);
    }
    return "";
}

int32_t WiFiClass::RSSI(uint8_t i) {
    if (i < _scanCount && _scanResult) {
        return _scanResult[i].rssi;
    }
    return 0;
}

wifi_enc_type_t WiFiClass::encryptionType(uint8_t i) {
    if (i < _scanCount && _scanResult) {
        switch (_scanResult[i].authmode) {
            case WIFI_AUTH_OPEN: return WIFI_AUTH_OPEN_IDF;
            case WIFI_AUTH_WEP: return WIFI_AUTH_WEP_IDF;
            case WIFI_AUTH_WPA_PSK: return WIFI_AUTH_WPA_PSK_IDF;
            default: return WIFI_AUTH_WPA2_PSK_IDF;
        }
    }
    return WIFI_AUTH_OPEN_IDF;
}

std::string WiFiClass::BSSIDstr(uint8_t i) {
    if (i < _scanCount && _scanResult) {
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", _scanResult[i].bssid[0], _scanResult[i].bssid[1],
                 _scanResult[i].bssid[2], _scanResult[i].bssid[3], _scanResult[i].bssid[4], _scanResult[i].bssid[5]);
        return std::string(buf);
    }
    return "";
}

int32_t WiFiClass::channel(uint8_t i) {
    if (i < _scanCount && _scanResult) {
        return _scanResult[i].primary;
    }
    return 0;
}

void WiFiClass::getNetworkInfo(uint8_t i, std::string& ssid, uint8_t& encType, int32_t& rssi, uint8_t*& bssid,
                               int32_t& ch) {
    if (i < _scanCount && _scanResult) {
        ssid = std::string((char*)_scanResult[i].ssid);
        encType = static_cast<uint8_t>(encryptionType(i));
        rssi = _scanResult[i].rssi;
        bssid = _scanResult[i].bssid;
        ch = _scanResult[i].primary;
    }
}

bool WiFiClass::softAP(const char* ssid, const char* password, int channel, bool ssid_hidden, int max_connection) {
    if (!_initialized) init();

    wifi_mode_t currentMode = getMode();
    if (currentMode == WIFI_MODE_NULL || currentMode == WIFI_MODE_STA) {
        mode(currentMode == WIFI_MODE_STA ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    }

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(ssid);
    wifi_config.ap.channel = channel;
    wifi_config.ap.ssid_hidden = ssid_hidden ? 1 : 0;
    wifi_config.ap.max_connection = max_connection;

    if (password && strlen(password) > 0) {
        strncpy((char*)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

bool WiFiClass::softAPConfig(IPAddress local_ip, IPAddress gateway, IPAddress subnet) {
    if (!_ap_netif) return false;

    esp_netif_dhcps_stop(_ap_netif);

    esp_netif_ip_info_t ip_info = {};
    ip_info.ip = local_ip;
    ip_info.gw = gateway;
    ip_info.netmask = subnet;

    esp_err_t err = esp_netif_set_ip_info(_ap_netif, &ip_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP IP info: %s", esp_err_to_name(err));
        return false;
    }

    esp_netif_dhcps_start(_ap_netif);
    return true;
}

bool WiFiClass::softAPdisconnect(bool wifiOff) {
    wifi_mode_t m = getMode();
    if (wifiOff) {
        if (m == WIFI_MODE_APSTA) {
            return mode(WIFI_MODE_STA);
        } else if (m == WIFI_MODE_AP) {
            return mode(WIFI_MODE_NULL);
        }
    }
    return true;
}

IPAddress WiFiClass::softAPIP() {
    esp_netif_ip_info_t ip_info;
    if (_ap_netif && esp_netif_get_ip_info(_ap_netif, &ip_info) == ESP_OK) {
        return IPAddress(ip_info.ip);
    }
    return IPAddress();
}

std::string WiFiClass::softAPmacAddress() {
    uint8_t mac[6];
    char buf[18];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buf);
}

uint8_t WiFiClass::softAPgetStationNum() {
    wifi_sta_list_t sta_list;
    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        return sta_list.num;
    }
    return 0;
}

bool WiFiClass::setAutoReconnect(bool autoReconnect) {
    _autoReconnect = autoReconnect;
    return true;
}

bool WiFiClass::persistent(bool persistent) {
    _persistent = persistent;
    if (_initialized) {
        return esp_wifi_set_storage(persistent ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM) == ESP_OK;
    }
    return true;
}

bool WiFiClass::setTxPower(int8_t power) { return esp_wifi_set_max_tx_power(power * 4) == ESP_OK; }
