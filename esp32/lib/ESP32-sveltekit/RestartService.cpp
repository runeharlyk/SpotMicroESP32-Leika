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

#include <RestartService.h>

RestartService::RestartService(PsychicHttpServer *server) : _server(server) {}

void RestartService::begin() {
    _server->on(RESTART_SERVICE_PATH, HTTP_POST, [this](PsychicRequest *request) { return restart(request); });

    ESP_LOGV("RestartService", "Registered POST endpoint: %s", RESTART_SERVICE_PATH);
}

esp_err_t RestartService::restart(PsychicRequest *request) {
    request->reply(200);
    restartNow();
    return ESP_OK;
}
