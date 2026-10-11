#pragma once
// Host stand-in for the GPIO driver: every pin reads high, as an idle I2C line with its pull-up does, unless a test
// holds it low.
#include <set>

typedef int gpio_num_t;
#define GPIO_NUM_NC -1

namespace fake_gpio {
inline std::set<gpio_num_t> low;
} // namespace fake_gpio

inline int gpio_get_level(gpio_num_t pin) { return fake_gpio::low.count(pin) ? 0 : 1; }
