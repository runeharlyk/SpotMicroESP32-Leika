#pragma once

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

#include <Arduino.h>

#include <WiFi.h>
#include <ArduinoJson.h>
#include <EventSocket.h>
#include <PsychicHttp.h>

#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <TaskManager.h>
// #include <SSLCertBundle.h>

#define GITHUB_FIRMWARE_PATH "/api/downloadUpdate"
#define EVENT_DOWNLOAD_OTA "otastatus"
#define OTA_TASK_STACK_SIZE 9216

class DownloadFirmwareService {
  public:
    DownloadFirmwareService(PsychicHttpServer *server, EventSocket *socket, TaskManager *taskManager);

    void begin();

  private:
    PsychicHttpServer *_server;
    EventSocket *_socket;
    TaskManager *_taskManager;
    esp_err_t downloadUpdate(PsychicRequest *request, JsonVariant &json);
};
