#ifndef NTPStatus_h
#define NTPStatus_h

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

#include <time.h>
#include <WiFi.h>
#include <lwip/apps/sntp.h>

#include <ArduinoJson.h>
#include <PsychicHttp.h>

#define NTP_STATUS_SERVICE_PATH "/api/ntpStatus"

class NTPStatus {
  public:
    NTPStatus(PsychicHttpServer *server);

    void begin();

  private:
    PsychicHttpServer *_server;
    esp_err_t ntpStatus(PsychicRequest *request);
};

#endif // end NTPStatus_h
