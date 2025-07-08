#pragma once

#include <WiFi.h>
#include <IPAddress.h>
#include <ArduinoJson.h>
#include <utils/json_utils.h>
#include <utils/string_utils.h>
#include <utils/ip_utils.h>
#include <template/state_result.h>

#include <DNSServer.h>
#include <IPAddress.h>

#ifndef FACTORY_AP_PROVISION_MODE
#define FACTORY_AP_PROVISION_MODE AP_MODE_DISCONNECTED
#endif

#ifndef FACTORY_AP_SSID
#define FACTORY_AP_SSID "ESP32-SvelteKit-#{unique_id}"
#endif

#ifndef FACTORY_AP_PASSWORD
#define FACTORY_AP_PASSWORD "esp-sveltekit"
#endif

#ifndef FACTORY_AP_LOCAL_IP
#define FACTORY_AP_LOCAL_IP "192.168.4.1"
#endif

#ifndef FACTORY_AP_GATEWAY_IP
#define FACTORY_AP_GATEWAY_IP "192.168.4.1"
#endif

#ifndef FACTORY_AP_SUBNET_MASK
#define FACTORY_AP_SUBNET_MASK "255.255.255.0"
#endif

#ifndef FACTORY_AP_CHANNEL
#define FACTORY_AP_CHANNEL 1
#endif

#ifndef FACTORY_AP_SSID_HIDDEN
#define FACTORY_AP_SSID_HIDDEN false
#endif

#ifndef FACTORY_AP_MAX_CLIENTS
#define FACTORY_AP_MAX_CLIENTS 4
#endif

#define AP_MODE_ALWAYS 0
#define AP_MODE_DISCONNECTED 1
#define AP_MODE_NEVER 2

#define MANAGE_NETWORK_DELAY 10000
#define DNS_PORT 53

enum APNetworkStatus { ACTIVE = 0, INACTIVE, LINGERING };

class APSettings {
  public:
    uint8_t provisionMode;
    String ssid;
    String password;
    uint8_t channel;
    bool ssidHidden;
    uint8_t maxClients;

    IPAddress localIP;
    IPAddress gatewayIP;
    IPAddress subnetMask;

    bool operator==(const APSettings &settings) const {
        return provisionMode == settings.provisionMode && ssid == settings.ssid && password == settings.password &&
               channel == settings.channel && ssidHidden == settings.ssidHidden && maxClients == settings.maxClients &&
               localIP == settings.localIP && gatewayIP == settings.gatewayIP && subnetMask == settings.subnetMask;
    }

    static void read(APSettings &settings, JsonObject &root) {
        root["provision_mode"] = settings.provisionMode;
        root["ssid"] = settings.ssid;
        root["password"] = settings.password;
        root["channel"] = settings.channel;
        root["ssid_hidden"] = settings.ssidHidden;
        root["max_clients"] = settings.maxClients;
        root["local_ip"] = settings.localIP.toString();
        root["gateway_ip"] = settings.gatewayIP.toString();
        root["subnet_mask"] = settings.subnetMask.toString();
    }

    static StateUpdateResult update(JsonObject &root, APSettings &settings) {
        APSettings newSettings = {};
        newSettings.provisionMode = root["provision_mode"] | FACTORY_AP_PROVISION_MODE;
        switch (settings.provisionMode) {
            case AP_MODE_ALWAYS:
            case AP_MODE_DISCONNECTED:
            case AP_MODE_NEVER: break;
            default: newSettings.provisionMode = AP_MODE_DISCONNECTED;
        }
        newSettings.ssid = root["ssid"] | format(FACTORY_AP_SSID);
        newSettings.password = root["password"] | FACTORY_AP_PASSWORD;
        newSettings.channel = root["channel"] | FACTORY_AP_CHANNEL;
        newSettings.ssidHidden = root["ssid_hidden"] | FACTORY_AP_SSID_HIDDEN;
        newSettings.maxClients = root["max_clients"] | FACTORY_AP_MAX_CLIENTS;

        JsonUtils::readIP(root, "local_ip", newSettings.localIP, FACTORY_AP_LOCAL_IP);
        JsonUtils::readIP(root, "gateway_ip", newSettings.gatewayIP, FACTORY_AP_GATEWAY_IP);
        JsonUtils::readIP(root, "subnet_mask", newSettings.subnetMask, FACTORY_AP_SUBNET_MASK);

        if (newSettings == settings) {
            return StateUpdateResult::UNCHANGED;
        }
        settings = newSettings;
        return StateUpdateResult::CHANGED;
    }
};