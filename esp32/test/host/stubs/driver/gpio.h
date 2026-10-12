#pragma once
// Host stand-in for the GPIO driver.
// On open-drain I2C lines a line reads low while the ESP32 or a device pulls it low, and high otherwise, as its pull-up
// makes it. The validity checks are the ESP32-S3's: GPIOs 0-21 and 26-48 exist, and every one can drive; a test marks
// pins input-only with gpio_stub::inputOnly.
#include <cstdint>
#include <esp_bit_defs.h>
#include <map>
#include <set>

namespace gpio_stub {
inline uint64_t inputOnly = 0;
}

typedef int gpio_num_t;
#define GPIO_NUM_NC -1
#define GPIO_IS_VALID_GPIO(pin) ((pin) >= 0 && (pin) <= 48 && ((pin) <= 21 || (pin) >= 26))
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) (GPIO_IS_VALID_GPIO(pin) && !(gpio_stub::inputOnly & BIT64(pin)))

typedef int gpio_mode_t;
#define GPIO_MODE_INPUT_OUTPUT_OD 7

namespace fake_gpio {
// Lines a device holds low.
inline std::set<gpio_num_t> low;
// What the ESP32 drives each pin to: 0 pulls the line low, 1 lets it go.
inline std::map<gpio_num_t, int> driven;
// A device cut off mid-read holds `data` low until `clock` has risen `pulsesLeft` more times.
struct StuckDevice {
    gpio_num_t data = GPIO_NUM_NC;
    gpio_num_t clock = GPIO_NUM_NC;
    int pulsesLeft = 0;
};
inline StuckDevice stuck;
inline int clockPulses = 0;
// STOP conditions seen: the data line rising while the clock line is high.
inline int stops = 0;

inline void reset() {
    low.clear();
    driven.clear();
    stuck = {};
    clockPulses = stops = 0;
}
} // namespace fake_gpio

inline int gpio_get_level(gpio_num_t pin) {
    auto driving = fake_gpio::driven.find(pin);
    const bool pulledByEsp = driving != fake_gpio::driven.end() && driving->second == 0;
    return fake_gpio::low.count(pin) || pulledByEsp ? 0 : 1;
}

inline int gpio_set_direction(gpio_num_t, gpio_mode_t) { return 0; }

inline int gpio_set_level(gpio_num_t pin, uint32_t level) {
    const int before = gpio_get_level(pin);
    fake_gpio::driven[pin] = level ? 1 : 0;
    const bool rose = before == 0 && gpio_get_level(pin) == 1;
    auto &stuck = fake_gpio::stuck;
    if (rose && pin == stuck.clock) {
        fake_gpio::clockPulses++;
        if (stuck.pulsesLeft > 0 && --stuck.pulsesLeft == 0) fake_gpio::low.erase(stuck.data);
    }
    if (rose && pin == stuck.data && gpio_get_level(stuck.clock) == 1) fake_gpio::stops++;
    return 0;
}
