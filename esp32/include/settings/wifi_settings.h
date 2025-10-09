#pragma once

#include <WiFi.h>
#include <ArduinoJson.h>
#include <utils/json_utils.h>
#include <template/state_result.h>
#include <string>

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

    void serialize(JsonVariant &json) const {
        json["ssid"] = ssid.c_str();
        json["password"] = password.c_str();
        json["static_ip_config"] = staticIPConfig;
        if (staticIPConfig) {
            JsonUtils::writeIP(json, "local_ip", localIP);
            JsonUtils::writeIP(json, "gateway_ip", gatewayIP);
            JsonUtils::writeIP(json, "subnet_mask", subnetMask);
            JsonUtils::writeIP(json, "dns_ip_1", dnsIP1);
            JsonUtils::writeIP(json, "dns_ip_2", dnsIP2);
        }
    }

    bool deserialize(const JsonVariant &json) {
        std::string newSsid = json["ssid"].as<std::string>();
        std::string newPassword = json["password"].as<std::string>();
        if (newSsid.length() < 1 || newSsid.length() > 31 || newPassword.length() > 64) {
            ESP_LOGE("WiFiSettings", "SSID or password length is invalid");
            return false;
        }
        ssid = newSsid;
        password = newPassword;
        staticIPConfig = json["static_ip_config"] | false;
        if (staticIPConfig) {
            JsonUtils::readIP(json, "local_ip", localIP);
            JsonUtils::readIP(json, "gateway_ip", gatewayIP);
            JsonUtils::readIP(json, "subnet_mask", subnetMask);
            JsonUtils::readIP(json, "dns_ip_1", dnsIP1);
            JsonUtils::readIP(json, "dns_ip_2", dnsIP2);
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
    static void read(WiFiSettings &settings, JsonVariant &root) {
        root["hostname"] = settings.hostname.c_str();
        root["priority_RSSI"] = settings.priorityBySignalStrength;
        JsonArray wifiNetworks = root["wifi_networks"].to<JsonArray>();
        for (const auto &wifi : settings.wifiSettings) {
            JsonVariant wifiNetwork = wifiNetworks.add<JsonVariant>();
            wifi.serialize(wifiNetwork);
        }
        ESP_LOGV("WiFiSettings", "WiFi Settings read");
    }
    static StateUpdateResult update(JsonVariant &root, WiFiSettings &settings) {
        settings.hostname = root["hostname"] | FACTORY_WIFI_HOSTNAME;
        settings.priorityBySignalStrength = root["priority_RSSI"] | true;
        settings.wifiSettings.clear();
        if (root["wifi_networks"].is<JsonArray>()) {
            JsonArray wifiNetworks = root["wifi_networks"];
            int networkCount = 0;
            for (JsonVariant wifiNetwork : wifiNetworks) {
                if (networkCount >= 5) {
                    ESP_LOGE("WiFiSettings", "Too many wifi networks");
                    break;
                }
                wifi_settings_t newSettings;
                if (newSettings.deserialize(wifiNetwork)) {
                    settings.wifiSettings.push_back(newSettings);
                    networkCount++;
                }
            }
        } else if (std::string(FACTORY_WIFI_SSID).length() > 0) {
            ESP_LOGI("WiFiSettings", "No WiFi config found - using factory settings");
            settings.wifiSettings.push_back(createDefaultWiFiSettings());
        }
        ESP_LOGV("WiFiSettings", "WiFi Settings updated");
        return StateUpdateResult::CHANGED;
    }
};