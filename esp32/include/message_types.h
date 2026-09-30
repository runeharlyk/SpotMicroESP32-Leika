#pragma once

#include <platform_shared/message.pb.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

/**
 * 0 for NaN and infinity, else the value. -Ofast implies -ffinite-math-only, under which std::isfinite
 * may be folded to true; the exponent bits cannot be.
 */
inline float finiteOrZero(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) == 0x7f800000u ? 0.0f : value;
}

inline float bounded(float value, float low, float high) { return std::clamp(finiteOrZero(value), low, high); }

/** Controller input as the motion states use it: sticks in [-1, 1], height, speed and s1 in [0, 1]. */
struct CommandMsg {
    float lx, ly, rx, ry, h, s, s1;

    void fromProto(const socket_message_ControllerData& data) {
        lx = data.has_left ? bounded(data.left.x, -1, 1) : 0;
        ly = data.has_left ? bounded(data.left.y, -1, 1) : 0;
        rx = data.has_right ? bounded(data.right.x, -1, 1) : 0;
        ry = data.has_right ? bounded(data.right.y, -1, 1) : 0;
        h = bounded(data.height, 0, 1);
        s = bounded(data.speed, 0, 1);
        s1 = bounded(data.s1, 0, 1);
    }

    /** Whether a stick is off centre, i.e. the robot is being driven. */
    bool steering() const { return lx != 0 || ly != 0 || rx != 0 || ry != 0; }
};
