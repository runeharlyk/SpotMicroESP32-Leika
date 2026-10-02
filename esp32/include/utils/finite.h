#pragma once

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

inline bool isFinite(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

inline float bounded(float value, float low, float high) { return std::clamp(finiteOrZero(value), low, high); }
