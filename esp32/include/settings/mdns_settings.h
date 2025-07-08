#pragma once

#include <ArduinoJson.h>
#include <utils/json_utils.h>
#include <utils/string_utils.h>
#include <template/state_result.h>
#include <filesystem.h>

#ifndef FACTORY_MDNS_HOSTNAME
#define FACTORY_MDNS_HOSTNAME "esp32"
#endif

#ifndef FACTORY_MDNS_INSTANCE
#define FACTORY_MDNS_INSTANCE "ESP32 Device"
#endif

typedef struct {
    String key;
    String value;

    void serialize(JsonObject &json) const {
        json["key"] = key;
        json["value"] = value;
    }

    bool deserialize(const JsonObject &json) {
        key = json["key"].as<String>();
        value = json["value"].as<String>();

        return key.length() > 0;
    }
} mdns_txt_record_t;

typedef struct {
    String service;
    String protocol;
    uint16_t port;
    std::vector<mdns_txt_record_t> txtRecords;

    void serialize(JsonObject &json) const {
        json["service"] = service;
        json["protocol"] = protocol;
        json["port"] = port;

        if (txtRecords.size() > 0) {
            JsonArray txtArray = json["txt_records"].to<JsonArray>();
            for (const auto &txt : txtRecords) {
                JsonObject txtObj = txtArray.add<JsonObject>();
                txt.serialize(txtObj);
            }
        }
    }

    bool deserialize(const JsonObject &json) {
        service = json["service"].as<String>();
        protocol = json["protocol"].as<String>();
        port = json["port"] | 0;

        txtRecords.clear();
        if (json["txt_records"].is<JsonArray>()) {
            JsonArray txtArray = json["txt_records"];
            for (JsonObject txtObj : txtArray) {
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
    String hostname;
    String instance;
    std::vector<mdns_service_t> services;
    std::vector<mdns_txt_record_t> globalTxtRecords;

    static void read(MDNSSettings &settings, JsonObject &root) {
        root["hostname"] = settings.hostname;
        root["instance"] = settings.instance;

        JsonArray servicesArray = root["services"].to<JsonArray>();
        for (const auto &service : settings.services) {
            JsonObject serviceObj = servicesArray.add<JsonObject>();
            service.serialize(serviceObj);
        }

        JsonArray txtArray = root["global_txt_records"].to<JsonArray>();
        for (const auto &txt : settings.globalTxtRecords) {
            JsonObject txtObj = txtArray.add<JsonObject>();
            txt.serialize(txtObj);
        }
    }

    static StateUpdateResult update(JsonObject &root, MDNSSettings &settings) {
        settings.hostname = root["hostname"] | FACTORY_MDNS_HOSTNAME;
        settings.instance = root["instance"] | FACTORY_MDNS_INSTANCE;

        settings.services.clear();
        if (root["services"].is<JsonArray>()) {
            JsonArray servicesArray = root["services"];
            for (JsonObject serviceObj : servicesArray) {
                mdns_service_t service;
                if (service.deserialize(serviceObj)) {
                    settings.services.push_back(service);
                }
            }
        }

        if (settings.services.empty()) {
            mdns_service_t httpService = {.service = "http", .protocol = "tcp", .port = 80};
            settings.services.push_back(httpService);

            mdns_service_t wsService = {.service = "ws", .protocol = "tcp", .port = 80};
            settings.services.push_back(wsService);
        }

        settings.globalTxtRecords.clear();
        if (root["global_txt_records"].is<JsonArray>()) {
            JsonArray txtArray = root["global_txt_records"];
            for (JsonObject txtObj : txtArray) {
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