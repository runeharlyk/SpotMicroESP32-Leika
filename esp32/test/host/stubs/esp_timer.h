#pragma once
// Host stand-in: a clock the test sets, or one that runs on its own from `source`.
#include <cstdint>

namespace fake_timer {
inline int64_t nowUs = 0;
// When set, every read takes the time from it, and nowUs follows.
inline int64_t (*source)() = nullptr;
}

inline int64_t esp_timer_get_time() { return fake_timer::source ? fake_timer::nowUs = fake_timer::source() : fake_timer::nowUs; }
