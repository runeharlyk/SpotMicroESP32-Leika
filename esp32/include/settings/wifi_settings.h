#pragma once

#include <WiFi.h>
#include <template/state_result.h>
#include <platform_shared/api.pb.h>
#include <cstring>

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

using WiFiNetwork = api_WifiNetwork;
using WiFiSettings = api_WifiSettings;

inline WiFiNetwork WiFiNetwork_defaults() {
    WiFiNetwork network = api_WifiNetwork_init_zero;
    strncpy(network.ssid, FACTORY_WIFI_SSID, sizeof(network.ssid) - 1);
    strncpy(network.password, FACTORY_WIFI_PASSWORD, sizeof(network.password) - 1);
    network.static_ip_config = false;
    network.local_ip = 0;
    network.gateway_ip = 0;
    network.subnet_mask = 0;
    network.dns_ip_1 = 0;
    network.dns_ip_2 = 0;
    return network;
}

inline WiFiSettings WiFiSettings_defaults() {
    WiFiSettings settings = api_WifiSettings_init_zero;
    strncpy(settings.hostname, FACTORY_WIFI_HOSTNAME, sizeof(settings.hostname) - 1);
    settings.priority_rssi = true;
    settings.wifi_networks_count = 0;
    if (strlen(FACTORY_WIFI_SSID) > 0) {
        settings.wifi_networks[0] = WiFiNetwork_defaults();
        settings.wifi_networks_count = 1;
    }
    return settings;
}

inline void WiFiSettings_read(const WiFiSettings &settings, WiFiSettings &proto) {
    proto = settings;
}

inline StateUpdateResult WiFiSettings_update(const WiFiSettings &proto, WiFiSettings &settings) {
    settings = proto;
    return StateUpdateResult::CHANGED;
}
