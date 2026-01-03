#pragma once

#include <WiFi.h>
#include <template/state_result.h>
#include <platform_shared/message.pb.h>
#include <string>
#include <vector>

#ifndef FACTORY_WIFI_SSID
#define FACTORY_WIFI_SSID ""
#endif

#ifndef FACTORY_WIFI_PASSWORD
#define FACTORY_WIFI_PASSWORD ""
#endif

#ifndef FACTORY_WIFI_HOSTNAME
#define FACTORY_WIFI_HOSTNAME "#{platform}-#{unique_id}"
#endif

#ifndef FACTORY_WIFI_RSSI_THRESHOLD
#define FACTORY_WIFI_RSSI_THRESHOLD -80
#endif

typedef struct {
    std::string ssid;
    uint8_t bssid[6];
    int32_t channel;
    std::string password;
    bool staticIPConfig;
    IPAddress localIP;
    IPAddress gatewayIP;
    IPAddress subnetMask;
    IPAddress dnsIP1;
    IPAddress dnsIP2;
    bool available;

    void toProto(socket_message_KnownNetworkItem& item) const {
        strlcpy(item.ssid, ssid.c_str(), sizeof(item.ssid));
        strlcpy(item.password, password.c_str(), sizeof(item.password));
        item.static_ip = staticIPConfig;
        if (staticIPConfig) {
            item.local_ip = (uint32_t)(localIP);
            item.gateway_ip = (uint32_t)(gatewayIP);
            item.subnet_mask = (uint32_t)(subnetMask);
            item.dns_ip_1 = (uint32_t)(dnsIP1);
            item.dns_ip_2 = (uint32_t)(dnsIP2);
        }
    }

    bool fromProto(const socket_message_KnownNetworkItem& item) {
        ssid = item.ssid;
        password = item.password;
        if (ssid.length() < 1 || ssid.length() > 31 || password.length() > 64) {
            ESP_LOGE("WiFiSettings", "SSID or password length is invalid");
            return false;
        }
        staticIPConfig = item.static_ip;
        if (staticIPConfig) {
            localIP = IPAddress(item.local_ip);
            gatewayIP = IPAddress(item.gateway_ip);
            subnetMask = IPAddress(item.subnet_mask);
            dnsIP1 = IPAddress(item.dns_ip_1);
            dnsIP2 = IPAddress(item.dns_ip_2);
            if (dnsIP1 == IPAddress(0, 0, 0, 0) && dnsIP2 != IPAddress(0, 0, 0, 0)) {
                dnsIP1 = dnsIP2;
                dnsIP2 = IPAddress(0, 0, 0, 0);
            }
            if (localIP == IPAddress(0, 0, 0, 0) || gatewayIP == IPAddress(0, 0, 0, 0) ||
                subnetMask == IPAddress(0, 0, 0, 0)) {
                staticIPConfig = false;
                ESP_LOGW("WiFiSettings", "Invalid static IP configuration - falling back to DHCP");
            }
        }
        available = false;
        return true;
    }
} wifi_settings_t;

inline wifi_settings_t createDefaultWiFiSettings() {
    return wifi_settings_t {
        .ssid = FACTORY_WIFI_SSID,
        .bssid = {0},
        .channel = -1,
        .password = FACTORY_WIFI_PASSWORD,
        .staticIPConfig = false,
        .localIP = IPAddress(0, 0, 0, 0),
        .gatewayIP = IPAddress(0, 0, 0, 0),
        .subnetMask = IPAddress(0, 0, 0, 0),
        .dnsIP1 = IPAddress(0, 0, 0, 0),
        .dnsIP2 = IPAddress(0, 0, 0, 0),
        .available = false,
    };
}

class WiFiSettings {
  public:
    std::string hostname;
    bool priorityBySignalStrength;
    std::vector<wifi_settings_t> wifiSettings;

    static void read(const WiFiSettings& settings, socket_message_WifiSettingsData& proto) {
        strlcpy(proto.hostname, settings.hostname.c_str(), sizeof(proto.hostname));
        proto.priority_rssi = settings.priorityBySignalStrength;
        proto.wifi_networks_count = std::min((size_t)8, settings.wifiSettings.size());
        for (size_t i = 0; i < proto.wifi_networks_count; i++) {
            settings.wifiSettings[i].toProto(proto.wifi_networks[i]);
        }
        ESP_LOGV("WiFiSettings", "WiFi Settings read");
    }

    static StateUpdateResult update(const socket_message_WifiSettingsData& proto, WiFiSettings& settings) {
        settings.hostname = strlen(proto.hostname) > 0 ? proto.hostname : FACTORY_WIFI_HOSTNAME;
        settings.priorityBySignalStrength = proto.priority_rssi;
        settings.wifiSettings.clear();

        for (size_t i = 0; i < proto.wifi_networks_count && i < 8; i++) {
            wifi_settings_t newSettings;
            if (newSettings.fromProto(proto.wifi_networks[i])) {
                settings.wifiSettings.push_back(newSettings);
            }
        }

        if (settings.wifiSettings.empty() && std::string(FACTORY_WIFI_SSID).length() > 0) {
            ESP_LOGI("WiFiSettings", "No WiFi config found - using factory settings");
            settings.wifiSettings.push_back(createDefaultWiFiSettings());
        }
        ESP_LOGV("WiFiSettings", "WiFi Settings updated");
        return StateUpdateResult::CHANGED;
    }
};
