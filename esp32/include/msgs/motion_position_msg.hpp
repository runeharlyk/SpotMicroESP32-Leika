#pragma once
#include <ArduinoJson.h>

struct MotionPositionMsg {
    float omega, phi, psi, xm, ym, zm;

    friend void toJson(JsonVariant v, MotionPositionMsg const &m) {
        JsonArray arr = v.to<JsonArray>();
        arr.add(m.omega);
        arr.add(m.phi);
        arr.add(m.psi);
        arr.add(m.xm);
        arr.add(m.ym);
        arr.add(m.zm);
    }

    void fromJson(JsonVariantConst o) {
        JsonArrayConst arr = o.as<JsonArrayConst>();
        omega = arr[0].as<float>();
        phi = arr[1].as<float>();
        psi = arr[2].as<float>();
        xm = arr[3].as<float>();
        ym = arr[4].as<float>();
        zm = arr[5].as<float>();
    }
};