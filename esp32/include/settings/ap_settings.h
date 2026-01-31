#pragma once

#include <wifi/wifi_idf.h>
#include <wifi/dns_server.h>
#include <ArduinoJson.h>
#include <template/state_result.h>
#include <platform_shared/api.pb.h>
#include <cstring>

#ifndef FACTORY_AP_PROVISION_MODE
#define FACTORY_AP_PROVISION_MODE api_APProvisionMode_AP_MODE_DISCONNECTED
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

#define AP_MODE_ALWAYS api_APProvisionMode_AP_MODE_ALWAYS
#define AP_MODE_DISCONNECTED api_APProvisionMode_AP_MODE_DISCONNECTED
#define AP_MODE_NEVER api_APProvisionMode_AP_MODE_NEVER

#define MANAGE_NETWORK_DELAY 10000
#define DNS_PORT 53

using APNetworkStatus = api_APNetworkStatus;
#define ACTIVE api_APNetworkStatus_AP_ACTIVE
#define INACTIVE api_APNetworkStatus_AP_INACTIVE
#define LINGERING api_APNetworkStatus_AP_LINGERING

inline uint32_t parseIPv4(const char *str) {
    IPAddress ip;
    ip.fromString(str);
    return (uint32_t)ip;
}

using APSettings = api_APSettings;

inline APSettings APSettings_defaults() {
    APSettings settings = {};
    settings.provision_mode = FACTORY_AP_PROVISION_MODE;
    strncpy(settings.ssid, FACTORY_AP_SSID, sizeof(settings.ssid) - 1);
    strncpy(settings.password, FACTORY_AP_PASSWORD, sizeof(settings.password) - 1);
    settings.channel = FACTORY_AP_CHANNEL;
    settings.ssid_hidden = FACTORY_AP_SSID_HIDDEN;
    settings.max_clients = FACTORY_AP_MAX_CLIENTS;
    settings.local_ip = parseIPv4(FACTORY_AP_LOCAL_IP);
    settings.gateway_ip = parseIPv4(FACTORY_AP_GATEWAY_IP);
    settings.subnet_mask = parseIPv4(FACTORY_AP_SUBNET_MASK);
    return settings;
}

inline void APSettings_read(const APSettings &settings, APSettings &proto) {
    proto = settings;
}

inline StateUpdateResult APSettings_update(const APSettings &proto, APSettings &settings) {
    settings = proto;
    return StateUpdateResult::CHANGED;
}