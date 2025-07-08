#pragma once
#include <ArduinoJson.h>

struct MotionAnglesMsg {
    float angles[12];

    friend void toJson(JsonVariant v, MotionAnglesMsg const &m) {
        JsonArray arr = v.to<JsonArray>();
        for (int i = 0; i < 12; i++) {
            arr.add(m.angles[i]);
        }
    }

    void fromJson(JsonVariantConst o) {
        JsonArrayConst arr = o.as<JsonArrayConst>();
        for (int i = 0; i < 12 && i < arr.size(); i++) {
            angles[i] = arr[i].as<float>();
        }
    }
};