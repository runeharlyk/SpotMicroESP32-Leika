#pragma once
#include <ArduinoJson.h>

enum class MotionState { DEACTIVATED, IDLE, CALIBRATION, REST, STAND, CRAWL, WALK };

struct ModeMsg {
    MotionState mode;
    friend void toJson(JsonVariant v, ModeMsg const &m) { v.set(static_cast<int>(m.mode)); }
    void fromJson(JsonVariantConst o) { mode = (MotionState)o.as<int>(); }
};