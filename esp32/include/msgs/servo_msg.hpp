#pragma once
#include <ArduinoJson.h>

#ifndef NUM_SERVOS
#define NUM_SERVOS 12
#endif

struct ServoMsg {
    float angles[NUM_SERVOS];
    friend void toJson(JsonVariant v, ServoMsg const &a) {
        JsonArray arr = v.to<JsonArray>();
        for (int i = 0; i < NUM_SERVOS; i++) {
            arr.add(a.angles[i]);
        }
    }
    void fromJson(JsonVariantConst o) {
        JsonArrayConst arr = o.as<JsonArrayConst>();
        for (int i = 0; i < NUM_SERVOS; i++) {
            angles[i] = arr[i].as<float>();
        }
    }
};