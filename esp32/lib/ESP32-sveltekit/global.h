#pragma once

#include <esp32-hal.h>

#if CONFIG_IDF_TARGET_ESP32 // ESP32/PICO-D4
#include "esp32/rom/rtc.h"
#ifndef ESP_PLATFORM
#define ESP_PLATFORM "ESP32"
#endif
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32/rom/rtc.h"
#ifndef ESP_PLATFORM
#define ESP_PLATFORM "ESP32-S2"
#endif
#elif CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/rtc.h"
#ifndef ESP_PLATFORM
#define ESP_PLATFORM "ESP32-C3"
#endif
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/rtc.h"
#ifndef ESP_PLATFORM
#define ESP_PLATFORM "ESP32-S3"
#endif
#else
#error Target CONFIG_IDF_TARGET is not supported
#endif

#ifndef ARDUINO_VERSION
#ifndef STRINGIFY
#define STRINGIFY(s) #s
#endif
#define ARDUINO_VERSION_STR(major, minor, patch) "v" STRINGIFY(major) "." STRINGIFY(minor) "." STRINGIFY(patch)
#define ARDUINO_VERSION \
    ARDUINO_VERSION_STR(ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH)
#endif

/*
 * I2C software connection
 */
#ifndef SDA_PIN
#define SDA_PIN SDA
#endif
#ifndef SCL_PIN
#define SCL_PIN SCL
#endif
#ifndef I2C_FREQUENCY
#define I2C_FREQUENCY 100000UL
#endif