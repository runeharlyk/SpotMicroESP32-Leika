#pragma once

#include <template/state_result.h>
#include <platform_shared/api.pb.h>
#include <string>
#include <cstring>

#ifndef FACTORY_MDNS_HOSTNAME
#define FACTORY_MDNS_HOSTNAME "esp32"
#endif

#ifndef FACTORY_MDNS_INSTANCE
#define FACTORY_MDNS_INSTANCE "ESP32 Device"
#endif

// Use proto types directly
using MDNSTxtRecord = api_MDNSTxtRecord;
using MDNSServiceDef = api_MDNSServiceDef;
using MDNSSettings = api_MDNSSettings;
using MDNSStatus = api_MDNSStatus;

// Default factory settings
inline MDNSSettings MDNSSettings_defaults() {
    MDNSSettings settings = api_MDNSSettings_init_zero;
    strncpy(settings.hostname, FACTORY_MDNS_HOSTNAME, sizeof(settings.hostname) - 1);
    strncpy(settings.instance, FACTORY_MDNS_INSTANCE, sizeof(settings.instance) - 1);

    // Default HTTP service
    settings.services_count = 2;
    strncpy(settings.services[0].service, "http", sizeof(settings.services[0].service) - 1);
    strncpy(settings.services[0].protocol, "tcp", sizeof(settings.services[0].protocol) - 1);
    settings.services[0].port = 80;
    settings.services[0].txt_records_count = 0;

    // Default WS service
    strncpy(settings.services[1].service, "ws", sizeof(settings.services[1].service) - 1);
    strncpy(settings.services[1].protocol, "tcp", sizeof(settings.services[1].protocol) - 1);
    settings.services[1].port = 80;
    settings.services[1].txt_records_count = 0;

    // Default global txt record
    settings.global_txt_records_count = 1;
    strncpy(settings.global_txt_records[0].key, "Firmware Version", sizeof(settings.global_txt_records[0].key) - 1);
    strncpy(settings.global_txt_records[0].value, APP_VERSION, sizeof(settings.global_txt_records[0].value) - 1);

    return settings;
}

// Proto read/update are identity functions since type is the same
inline void MDNSSettings_read(const MDNSSettings& settings, MDNSSettings& proto) {
    proto = settings;
}

inline StateUpdateResult MDNSSettings_update(const MDNSSettings& proto, MDNSSettings& settings) {
    settings = proto;
    return StateUpdateResult::CHANGED;
}
