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
#elif CONFIG_IDF_TARGET_ESP32C6
#include "esp32c6/rom/rtc.h"
#ifndef ESP_PLATFORM_NAME
#define ESP_PLATFORM_NAME "ESP32-C6"
#endif
#elif CONFIG_IDF_TARGET_ESP32P4
#include "esp32p4/rom/rtc.h"
#ifndef ESP_PLATFORM_NAME
#define ESP_PLATFORM_NAME "ESP32-P4"
#endif
#define ESP32P4_USES_C6_COPROCESSOR 1
#else
#error Target CONFIG_IDF_TARGET is not supported
#endif

/*
 * I2C software connection
 */
#if CONFIG_IDF_TARGET_ESP32P4
#ifndef SDA_PIN
#define SDA_PIN 7
#endif
#ifndef SCL_PIN
#define SCL_PIN 8
#endif
#else
#ifndef SDA_PIN
#define SDA_PIN 21
#endif
#ifndef SCL_PIN
#define SCL_PIN 22
#endif
#endif
#ifndef I2C_FREQUENCY
#define I2C_FREQUENCY 1000000UL
#endif
