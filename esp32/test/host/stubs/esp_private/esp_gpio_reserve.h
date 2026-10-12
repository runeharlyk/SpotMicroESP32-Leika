#pragma once
// Host stand-in for the GPIO reservations drivers make; a test sets the reserved mask.
#include <cstdint>

namespace gpio_stub {
inline uint64_t reserved = 0;
}

inline bool esp_gpio_is_reserved(uint64_t mask) { return (gpio_stub::reserved & mask) != 0; }
