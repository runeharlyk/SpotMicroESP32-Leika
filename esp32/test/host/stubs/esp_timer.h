#pragma once
// Host stand-in: a clock the test sets.
#include <cstdint>

namespace fake_timer {
inline int64_t nowUs = 0;
}

inline int64_t esp_timer_get_time() { return fake_timer::nowUs; }
