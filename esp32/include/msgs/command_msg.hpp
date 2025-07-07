#pragma once
#include <ArduinoJson.h>

struct CommandMsg {
    float x, y;
    friend void toJson(JsonVariant v, CommandMsg const &c) {
        JsonArray arr = v.to<JsonArray>();
        arr.add(c.x);
        arr.add(c.y);
    }

    void fromJson(JsonVariantConst o) {
        JsonArrayConst arr = o.as<JsonArrayConst>();
        x = arr[0].as<float>();
        y = arr[1].as<float>();
    }
};