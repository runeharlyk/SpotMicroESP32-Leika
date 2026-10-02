#pragma once

#include <wifi/wifi_idf.h>
#include <template/state_result.h>
#include <platform_shared/api.pb.h>
#include <settings/placeholders.h>
#include <cstring>

#ifndef FACTORY_WIFI_HOSTNAME
#define FACTORY_WIFI_HOSTNAME "#{platform}-#{unique_id}"
#endif

#ifndef FACTORY_WIFI_RSSI_THRESHOLD
#define FACTORY_WIFI_RSSI_THRESHOLD -80
#endif

using WiFiNetwork = api_WifiNetwork;
using WiFiSettings = api_WifiSettings;

inline WiFiSettings WiFiSettings_defaults() {
    WiFiSettings settings = api_WifiSettings_init_zero;
    strncpy(settings.hostname, toHostLabel(substitutePlaceholders(FACTORY_WIFI_HOSTNAME)).c_str(),
            sizeof(settings.hostname) - 1);
    settings.priority_rssi = true;
    settings.wifi_networks_count = 0;
    settings.selected_network = 0;
    return settings;
}

inline void WiFiSettings_read(const WiFiSettings &settings, WiFiSettings &proto) { proto = settings; }

inline StateUpdateResult WiFiSettings_update(const WiFiSettings &proto, WiFiSettings &settings) {
    settings = proto;
    return StateUpdateResult::CHANGED;
}
