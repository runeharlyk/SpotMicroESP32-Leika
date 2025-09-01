#pragma once

#include <ArduinoJson.h>

struct CommandMsg {
    float lx, ly, rx, ry, h, s, s1;
    friend void toJson(JsonVariant v, CommandMsg const &c) {
        JsonArray arr = v.to<JsonArray>();
        arr.add(c.lx);
        arr.add(c.ly);
        arr.add(c.rx);
        arr.add(c.ry);
        arr.add(c.h);
        arr.add(c.s);
        arr.add(c.s1);
    }

    void fromJson(JsonVariantConst o) {
        JsonArrayConst arr = o.as<JsonArrayConst>();
        lx = arr[0].as<float>();
        ly = arr[1].as<float>();
        rx = arr[2].as<float>();
        ry = arr[3].as<float>();
        h = arr[4].as<float>();
        s = arr[5].as<float>();
        s1 = arr[6].as<float>();
    }
};