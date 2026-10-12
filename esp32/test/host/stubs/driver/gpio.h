#pragma once
// Host stand-in for the ESP32-S3's GPIO validity checks: GPIOs 0-21 and 26-48 exist, and every one can drive. A test
// marks pins input-only with gpio_stub::inputOnly.
#include <cstdint>
#include <esp_bit_defs.h>

namespace gpio_stub {
inline uint64_t inputOnly = 0;
}

typedef int gpio_num_t;
#define GPIO_NUM_NC -1
#define GPIO_IS_VALID_GPIO(pin) ((pin) >= 0 && (pin) <= 48 && ((pin) <= 21 || (pin) >= 26))
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) (GPIO_IS_VALID_GPIO(pin) && !(gpio_stub::inputOnly & BIT64(pin)))
