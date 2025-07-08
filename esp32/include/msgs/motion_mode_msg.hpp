#pragma once
#include <ArduinoJson.h>

struct MotionModeMsg {
    int mode;

    friend void toJson(JsonVariant v, MotionModeMsg const &m) { v.set(m.mode); }

    void fromJson(JsonVariantConst o) { mode = o.as<int>(); }
};