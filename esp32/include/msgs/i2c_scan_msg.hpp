#pragma once
#include <ArduinoJson.h>
#include <vector>

struct I2CScanMsg {
    std::vector<uint8_t> addresses;

    friend void toJson(JsonVariant v, I2CScanMsg const& c) {
        JsonArray arr = v.to<JsonArray>();
        for (uint8_t addr : c.addresses) arr.add(addr);
    }

    void fromJson(JsonVariantConst o) {
        addresses.clear();
        for (JsonVariantConst val : o.as<JsonArrayConst>()) addresses.push_back(val.as<uint8_t>());
    }
};