#pragma once
#include <ArduinoJson.h>

struct ImuMsg {
    float ypr[3];
    friend void toJson(JsonVariant v, ImuMsg const &a) {
        JsonArray arr = v.to<JsonArray>();
        arr.add(a.ypr[0]);
        arr.add(a.ypr[1]);
        arr.add(a.ypr[2]);
    }
    void fromJson(JsonVariantConst o) {
        JsonArrayConst arr = o.as<JsonArrayConst>();
        ypr[0] = arr[0].as<float>();
        ypr[1] = arr[1].as<float>();
        ypr[2] = arr[2].as<float>();
    }
};