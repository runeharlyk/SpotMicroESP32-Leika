#pragma once

#include <sdkconfig.h>
#include <esp_system.h>

#if CONFIG_IDF_TARGET_ESP32
#include "esp32/rom/rtc.h"
#ifndef ESP_PLATFORM_NAME
#define ESP_PLATFORM_NAME "ESP32"
#endif
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/rom/rtc.h"
#ifndef ESP_PLATFORM_NAME
#define ESP_PLATFORM_NAME "ESP32-S2"
#endif
#elif CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/rtc.h"
#ifndef ESP_PLATFORM_NAME
#define ESP_PLATFORM_NAME "ESP32-C3"
#endif
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/rtc.h"
#ifndef ESP_PLATFORM_NAME
#define ESP_PLATFORM_NAME "ESP32-S3"
#endif
#else
#error Target CONFIG_IDF_TARGET is not supported
#endif

#ifndef SDA_PIN
#define SDA_PIN 21
#endif
#ifndef SCL_PIN
#define SCL_PIN 22
#endif
#ifndef I2C_FREQUENCY
#define I2C_FREQUENCY 100000UL
#endif
