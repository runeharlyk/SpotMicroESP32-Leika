#ifndef SystemStatus_h
#define SystemStatus_h

/**
 *   ESP32 SvelteKit
 *
 *   A simple, secure and extensible framework for IoT projects for ESP32 platforms
 *   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
 *   https://github.com/theelims/ESP32-sveltekit
 *
 *   Copyright (C) 2018 - 2023 rjwats
 *   Copyright (C) 2023 theelims
 *
 *   All Rights Reserved. This software may be modified and distributed under
 *   the terms of the LGPL v3 license. See the LICENSE file for details.
 **/

#include <WiFi.h>

#include <ArduinoJson.h>
#include <PsychicHttp.h>
#include <SecurityManager.h>
#include <ESPFS.h>
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
#ifndef STRINGIZE
#define STRINGIZE(s) #s
#endif
#define ARDUINO_VERSION_STR(major, minor, patch) "v" STRINGIZE(major) "." STRINGIZE(minor) "." STRINGIZE(patch)
#define ARDUINO_VERSION ARDUINO_VERSION_STR(ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH)
#endif


#define SYSTEM_STATUS_SERVICE_PATH "/api/systemStatus"

class SystemStatus
{
public:
    SystemStatus(PsychicHttpServer *server, SecurityManager *securityManager);

    void begin();

private:
    PsychicHttpServer *_server;
    SecurityManager *_securityManager;
    esp_err_t systemStatus(PsychicRequest *request);
};

#endif // end SystemStatus_h
