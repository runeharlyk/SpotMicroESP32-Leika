#pragma once

#include <ArduinoJson.h>
#include <template/state_result.h>
#include <filesystem.h>
#include <string>

#ifndef FACTORY_MDNS_HOSTNAME
#define FACTORY_MDNS_HOSTNAME "esp32"
#endif

#ifndef FACTORY_MDNS_INSTANCE
#define FACTORY_MDNS_INSTANCE "ESP32 Device"
#endif

typedef struct {
    std::string key;
    std::string value;

    void serialize(JsonVariant &json) const {
        json["key"] = key.c_str();
        json["value"] = value.c_str();
    }

    bool deserialize(const JsonVariant &json) {
        key = json["key"].as<std::string>();
        value = json["value"].as<std::string>();

        return key.length() > 0;
    }
} mdns_txt_record_t;

typedef struct {
    std::string service;
    std::string protocol;
    uint16_t port;
    std::vector<mdns_txt_record_t> txtRecords;

    void serialize(JsonVariant &json) const {
        json["service"] = service.c_str();
        json["protocol"] = protocol.c_str();
        json["port"] = port;

        if (txtRecords.size() > 0) {
            JsonArray txtArray = json["txt_records"].to<JsonArray>();
            for (const auto &txt : txtRecords) {
                JsonVariant txtObj = txtArray.add<JsonVariant>();
                txt.serialize(txtObj);
            }
        }
    }

    bool deserialize(const JsonVariant &json) {
        service = json["service"].as<std::string>();
        protocol = json["protocol"].as<std::string>();
        port = json["port"] | 0;

        txtRecords.clear();
        if (json["txt_records"].is<JsonArray>()) {
            JsonArray txtArray = json["txt_records"];
            for (JsonVariant txtObj : txtArray) {
                mdns_txt_record_t txt;
                if (txt.deserialize(txtObj)) {
                    txtRecords.push_back(txt);
                }
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

    static void read(MDNSSettings &settings, JsonVariant &root) {
        root["hostname"] = settings.hostname.c_str();
        root["instance"] = settings.instance.c_str();

        JsonArray servicesArray = root["services"].to<JsonArray>();
        for (const auto &service : settings.services) {
            JsonVariant serviceObj = servicesArray.add<JsonVariant>();
            service.serialize(serviceObj);
        }

        JsonArray txtArray = root["global_txt_records"].to<JsonArray>();
        for (const auto &txt : settings.globalTxtRecords) {
            JsonVariant txtObj = txtArray.add<JsonVariant>();
            txt.serialize(txtObj);
        }
    }

    static StateUpdateResult update(JsonVariant &root, MDNSSettings &settings) {
        settings.hostname = root["hostname"] | FACTORY_MDNS_HOSTNAME;
        settings.instance = root["instance"] | FACTORY_MDNS_INSTANCE;

        settings.services.clear();
        if (root["services"].is<JsonArray>()) {
            JsonArray servicesArray = root["services"];
            for (JsonVariant serviceObj : servicesArray) {
                mdns_service_t service;
                if (service.deserialize(serviceObj)) {
                    settings.services.push_back(service);
                }
            }
        }

        if (settings.services.empty()) {
            mdns_service_t httpService = {.service = "http", .protocol = "tcp", .port = 80, .txtRecords = {}};
            settings.services.push_back(httpService);

            mdns_service_t wsService = {.service = "ws", .protocol = "tcp", .port = 80, .txtRecords = {}};
            settings.services.push_back(wsService);
        }

        settings.globalTxtRecords.clear();
        if (root["global_txt_records"].is<JsonArray>()) {
            JsonArray txtArray = root["global_txt_records"];
            for (JsonVariant txtObj : txtArray) {
                mdns_txt_record_t txt;
                if (txt.deserialize(txtObj)) {
                    settings.globalTxtRecords.push_back(txt);
                }
            }
        }

        if (settings.globalTxtRecords.empty()) {
            mdns_txt_record_t firmwareVersion = {.key = "Firmware Version", .value = APP_VERSION};
            settings.globalTxtRecords.push_back(firmwareVersion);
        }

        return StateUpdateResult::CHANGED;
    }
};