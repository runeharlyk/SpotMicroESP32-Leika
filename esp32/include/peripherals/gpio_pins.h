#pragma once

#include <driver/gpio.h>
#include <esp_bit_defs.h>
#include <esp_private/esp_gpio_reserve.h>

/**
 * A GPIO the chip can drive that no driver holds, such as the flash, the PSRAM or the camera: a strip on one of
 * theirs could stop the firmware at every boot. A pin this firmware drives itself is held too.
 */
inline bool usableOutputPin(int32_t pin) {
    return pin >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(pin) && !esp_gpio_is_reserved(BIT64(pin));
}
