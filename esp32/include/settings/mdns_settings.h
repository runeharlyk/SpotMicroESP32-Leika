#pragma once

#include <template/state_result.h>
#include <platform_shared/message.pb.h>
#include <filesystem.h>
#include <string>
#include <vector>

#ifndef FACTORY_MDNS_HOSTNAME
#define FACTORY_MDNS_HOSTNAME "esp32"
#endif

#ifndef FACTORY_MDNS_INSTANCE
#define FACTORY_MDNS_INSTANCE "ESP32 Device"
#endif

typedef struct {
    std::string key;
    std::string value;

    void toProto(socket_message_MDNSTxtRecord& proto) const {
        strlcpy(proto.key, key.c_str(), sizeof(proto.key));
        strlcpy(proto.value, value.c_str(), sizeof(proto.value));
    }

    bool fromProto(const socket_message_MDNSTxtRecord& proto) {
        key = proto.key;
        value = proto.value;
        return key.length() > 0;
    }
} mdns_txt_record_t;

typedef struct {
    std::string service;
    std::string protocol;
    uint16_t port;
    std::vector<mdns_txt_record_t> txtRecords;

    void toProto(socket_message_MDNSServiceConfig& proto) const {
        strlcpy(proto.service, service.c_str(), sizeof(proto.service));
        strlcpy(proto.protocol, protocol.c_str(), sizeof(proto.protocol));
        proto.port = port;
        proto.txt_records_count = std::min((pb_size_t)10, (pb_size_t)txtRecords.size());
        for (pb_size_t i = 0; i < proto.txt_records_count; i++) {
            txtRecords[i].toProto(proto.txt_records[i]);
        }
    }

    bool fromProto(const socket_message_MDNSServiceConfig& proto) {
        service = proto.service;
        protocol = proto.protocol;
        port = proto.port;

        txtRecords.clear();
        for (pb_size_t i = 0; i < proto.txt_records_count; i++) {
            mdns_txt_record_t txt;
            if (txt.fromProto(proto.txt_records[i])) {
                txtRecords.push_back(txt);
            }
        }

        return service.length() > 0 && protocol.length() > 0 && port > 0;
    }
} mdns_service_t;

class MDNSSettings {
  public:
    std::string hostname;
    std::string instance;
    std::vector<mdns_service_t> services;
    std::vector<mdns_txt_record_t> globalTxtRecords;

    static void read(const MDNSSettings& settings, socket_message_MDNSSettingsData& proto) {
        strlcpy(proto.hostname, settings.hostname.c_str(), sizeof(proto.hostname));
        strlcpy(proto.instance, settings.instance.c_str(), sizeof(proto.instance));

        proto.services_count = std::min((pb_size_t)10, (pb_size_t)settings.services.size());
        for (pb_size_t i = 0; i < proto.services_count; i++) {
            settings.services[i].toProto(proto.services[i]);
        }

        proto.global_txt_records_count = std::min((pb_size_t)10, (pb_size_t)settings.globalTxtRecords.size());
        for (pb_size_t i = 0; i < proto.global_txt_records_count; i++) {
            settings.globalTxtRecords[i].toProto(proto.global_txt_records[i]);
        }
    }

    static StateUpdateResult update(const socket_message_MDNSSettingsData& proto, MDNSSettings& settings) {
        settings.hostname = strlen(proto.hostname) > 0 ? proto.hostname : FACTORY_MDNS_HOSTNAME;
        settings.instance = strlen(proto.instance) > 0 ? proto.instance : FACTORY_MDNS_INSTANCE;

        settings.services.clear();
        for (pb_size_t i = 0; i < proto.services_count; i++) {
            mdns_service_t service;
            if (service.fromProto(proto.services[i])) {
                settings.services.push_back(service);
            }
        }

        if (settings.services.empty()) {
            mdns_service_t httpService = {.service = "http", .protocol = "tcp", .port = 80, .txtRecords = {}};
            settings.services.push_back(httpService);

            mdns_service_t wsService = {.service = "ws", .protocol = "tcp", .port = 80, .txtRecords = {}};
            settings.services.push_back(wsService);
        }

        settings.globalTxtRecords.clear();
        for (pb_size_t i = 0; i < proto.global_txt_records_count; i++) {
            mdns_txt_record_t txt;
            if (txt.fromProto(proto.global_txt_records[i])) {
                settings.globalTxtRecords.push_back(txt);
            }
        }

        if (settings.globalTxtRecords.empty()) {
            mdns_txt_record_t firmwareVersion = {.key = "Firmware Version", .value = APP_VERSION};
            settings.globalTxtRecords.push_back(firmwareVersion);
        }

        return StateUpdateResult::CHANGED;
    }
};
