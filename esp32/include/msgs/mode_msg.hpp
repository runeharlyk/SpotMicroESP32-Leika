#pragma once
#include <ArduinoJson.h>

enum class MotionState { ON, OFF };

struct ModeMsg {
    MotionState mode;
    friend void toJson(JsonVariant v, ModeMsg const &m) { v.set(static_cast<int>(m.mode)); }
    void fromJson(JsonVariantConst o) { mode = (MotionState)o.as<int>(); }
};