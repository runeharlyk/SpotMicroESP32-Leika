#pragma once
#include <ArduinoJson.h>

struct MotionInputMsg {
    float lx, ly, rx, ry, h, s, s1;

    friend void toJson(JsonVariant v, MotionInputMsg const &m) {
        JsonArray arr = v.to<JsonArray>();
        arr.add(m.lx);
        arr.add(m.ly);
        arr.add(m.rx);
        arr.add(m.ry);
        arr.add(m.h);
        arr.add(m.s);
        arr.add(m.s1);
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