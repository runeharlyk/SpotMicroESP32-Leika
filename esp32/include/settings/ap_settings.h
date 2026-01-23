#pragma once

#include <WiFi.h>
#include <ArduinoJson.h>
#include <template/state_result.h>
#include <platform_shared/api.pb.h>
#include <string>
#include <cstring>

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

    bool operator==(const APSettings &settings) const {
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

    static StateUpdateResult update(JsonVariant &root, APSettings &settings) {
        APSettings newSettings = {};
        newSettings.provisionMode = root["provision_mode"] | FACTORY_AP_PROVISION_MODE;
        switch (settings.provisionMode) {
            case AP_MODE_ALWAYS:
            case AP_MODE_DISCONNECTED:
            case AP_MODE_NEVER: break;
            default: newSettings.provisionMode = AP_MODE_DISCONNECTED;
        }
        newSettings.ssid = root["ssid"] | FACTORY_AP_SSID;
        newSettings.password = root["password"] | FACTORY_AP_PASSWORD;
        newSettings.channel = root["channel"] | FACTORY_AP_CHANNEL;
        newSettings.ssidHidden = root["ssid_hidden"] | FACTORY_AP_SSID_HIDDEN;
        newSettings.maxClients = root["max_clients"] | FACTORY_AP_MAX_CLIENTS;

        newSettings.localIP = IPAddress(root["local_ip"] | parseIPv4(FACTORY_AP_LOCAL_IP));
        newSettings.gatewayIP = IPAddress(root["gateway_ip"] | parseIPv4(FACTORY_AP_GATEWAY_IP));
        newSettings.subnetMask = IPAddress(root["subnet_mask"] | parseIPv4(FACTORY_AP_SUBNET_MASK));

        if (newSettings == settings) {
            return StateUpdateResult::UNCHANGED;
        }
        settings = newSettings;
        return StateUpdateResult::CHANGED;
    }

    /** Converts internal state to protobuf message */
    static void readProto(const APSettings &settings, api_APSettings &proto) {
        proto.provision_mode = static_cast<api_APProvisionMode>(settings.provisionMode);
        strncpy(proto.ssid, settings.ssid.c_str(), sizeof(proto.ssid) - 1);
        proto.ssid[sizeof(proto.ssid) - 1] = '\0';
        strncpy(proto.password, settings.password.c_str(), sizeof(proto.password) - 1);
        proto.password[sizeof(proto.password) - 1] = '\0';
        proto.channel = settings.channel;
        proto.ssid_hidden = settings.ssidHidden;
        proto.max_clients = settings.maxClients;
        proto.local_ip = static_cast<uint32_t>(settings.localIP);
        proto.gateway_ip = static_cast<uint32_t>(settings.gatewayIP);
        proto.subnet_mask = static_cast<uint32_t>(settings.subnetMask);
    }

    /** Converts incoming protobuf message to internal state */
    static StateUpdateResult updateProto(const api_APSettings &proto, APSettings &settings) {
        APSettings newSettings = {};
        newSettings.provisionMode = static_cast<uint8_t>(proto.provision_mode);
        switch (newSettings.provisionMode) {
            case AP_MODE_ALWAYS:
            case AP_MODE_DISCONNECTED:
            case AP_MODE_NEVER: break;
            default: newSettings.provisionMode = AP_MODE_DISCONNECTED;
        }
        newSettings.ssid = proto.ssid[0] ? proto.ssid : FACTORY_AP_SSID;
        newSettings.password = proto.password[0] ? proto.password : FACTORY_AP_PASSWORD;
        newSettings.channel = proto.channel ? proto.channel : FACTORY_AP_CHANNEL;
        newSettings.ssidHidden = proto.ssid_hidden;
        newSettings.maxClients = proto.max_clients ? proto.max_clients : FACTORY_AP_MAX_CLIENTS;

        newSettings.localIP = proto.local_ip ? IPAddress(proto.local_ip) : IPAddress(parseIPv4(FACTORY_AP_LOCAL_IP));
        newSettings.gatewayIP = proto.gateway_ip ? IPAddress(proto.gateway_ip) : IPAddress(parseIPv4(FACTORY_AP_GATEWAY_IP));
        newSettings.subnetMask = proto.subnet_mask ? IPAddress(proto.subnet_mask) : IPAddress(parseIPv4(FACTORY_AP_SUBNET_MASK));

        if (newSettings == settings) {
            return StateUpdateResult::UNCHANGED;
        }
        settings = newSettings;
        return StateUpdateResult::CHANGED;
    }
};