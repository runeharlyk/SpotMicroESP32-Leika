#pragma once

#include <WiFi.h>
#include <template/state_result.h>
#include <platform_shared/message.pb.h>
#include <string>

#include <DNSServer.h>

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

inline uint32_t parseIPv4(const char *str) {
    IPAddress ip;
    ip.fromString(str);
    return (uint32_t)ip;
}

class APSettings {
  public:
    uint8_t provisionMode;
    std::string ssid;
    std::string password;
    uint8_t channel;
    bool ssidHidden;
    uint8_t maxClients;

    IPAddress localIP;
    IPAddress gatewayIP;
    IPAddress subnetMask;

    bool operator==(const APSettings& settings) const {
        return provisionMode == settings.provisionMode && ssid == settings.ssid && password == settings.password &&
               channel == settings.channel && ssidHidden == settings.ssidHidden && maxClients == settings.maxClients &&
               localIP == settings.localIP && gatewayIP == settings.gatewayIP && subnetMask == settings.subnetMask;
    }

    static void read(APSettings &settings, JsonVariant &root) {
        root["provision_mode"] = settings.provisionMode;
        root["ssid"] = settings.ssid.c_str();
        root["password"] = settings.password.c_str();
        root["channel"] = settings.channel;
        root["ssid_hidden"] = settings.ssidHidden;
        root["max_clients"] = settings.maxClients;
        root["local_ip"] = (uint32_t)(settings.localIP);
        root["gateway_ip"] = (uint32_t)(settings.gatewayIP);
        root["subnet_mask"] = (uint32_t)(settings.subnetMask);
    }

    static StateUpdateResult update(const socket_message_APSettingsData& proto, APSettings& settings) {
        APSettings newSettings = {};
        newSettings.provisionMode = proto.provision_mode;
        switch (newSettings.provisionMode) {
            case AP_MODE_ALWAYS:
            case AP_MODE_DISCONNECTED:
            case AP_MODE_NEVER: break;
            default: newSettings.provisionMode = AP_MODE_DISCONNECTED;
        }
        newSettings.ssid = strlen(proto.ssid) > 0 ? proto.ssid : FACTORY_AP_SSID;
        newSettings.password = strlen(proto.password) > 0 ? proto.password : FACTORY_AP_PASSWORD;
        newSettings.channel = proto.channel > 0 ? proto.channel : FACTORY_AP_CHANNEL;
        newSettings.ssidHidden = proto.ssid_hidden;
        newSettings.maxClients = proto.max_clients > 0 ? proto.max_clients : FACTORY_AP_MAX_CLIENTS;

        if (strlen(proto.local_ip) > 0) {
            newSettings.localIP.fromString(proto.local_ip);
        } else {
            newSettings.localIP.fromString(FACTORY_AP_LOCAL_IP);
        }
        if (strlen(proto.gateway_ip) > 0) {
            newSettings.gatewayIP.fromString(proto.gateway_ip);
        } else {
            newSettings.gatewayIP.fromString(FACTORY_AP_GATEWAY_IP);
        }
        if (strlen(proto.subnet_mask) > 0) {
            newSettings.subnetMask.fromString(proto.subnet_mask);
        } else {
            newSettings.subnetMask.fromString(FACTORY_AP_SUBNET_MASK);
        }
        newSettings.localIP = IPAddress(root["local_ip"] | parseIPv4(FACTORY_AP_LOCAL_IP));
        newSettings.gatewayIP = IPAddress(root["gateway_ip"] | parseIPv4(FACTORY_AP_GATEWAY_IP));
        newSettings.subnetMask = IPAddress(root["subnet_mask"] | parseIPv4(FACTORY_AP_SUBNET_MASK));

        if (newSettings == settings) {
            return StateUpdateResult::UNCHANGED;
        }
        settings = newSettings;
        return StateUpdateResult::CHANGED;
    }
};
