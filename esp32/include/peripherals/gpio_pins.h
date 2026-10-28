#pragma once

#include <sdkconfig.h>
#include <driver/gpio.h>
#include <esp_bit_defs.h>
#include <esp_private/esp_gpio_reserve.h>
#include <initializer_list>

#if USE_CAMERA && !CONFIG_IDF_TARGET_ESP32P4
#include <peripherals/camera_pins.h>
#endif

/**
 * A GPIO the chip can drive that no driver holds, such as the flash or the PSRAM: a strip on one of theirs could stop
 * the firmware at every boot. A pin this firmware drives through a driver that reserves it, such as RMT, is held too;
 * the camera's pins are not, see cameraPin().
 */
inline bool usableOutputPin(int32_t pin) {
    return pin >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(pin) && !esp_gpio_is_reserved(BIT64(pin));
}

/** A GPIO the chip can read that no driver holds. */
inline bool usableInputPin(int32_t pin) {
    return pin >= 0 && GPIO_IS_VALID_GPIO(pin) && !esp_gpio_is_reserved(BIT64(pin));
}

/** A pin of the board's parallel camera, whose driver does not reserve its pins. */
inline bool cameraPin(int32_t pin) {
#if USE_CAMERA && !CONFIG_IDF_TARGET_ESP32P4
    for (const int camera : {PWDN_GPIO_NUM, RESET_GPIO_NUM, XCLK_GPIO_NUM, SIOD_GPIO_NUM, SIOC_GPIO_NUM, Y9_GPIO_NUM,
                             Y8_GPIO_NUM, Y7_GPIO_NUM, Y6_GPIO_NUM, Y5_GPIO_NUM, Y4_GPIO_NUM, Y3_GPIO_NUM, Y2_GPIO_NUM,
                             VSYNC_GPIO_NUM, HREF_GPIO_NUM, PCLK_GPIO_NUM}) {
        if (pin >= 0 && pin == camera) return true;
    }
#endif
    return false;
}
