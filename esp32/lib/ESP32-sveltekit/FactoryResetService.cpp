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

#include <FactoryResetService.h>

FactoryResetService::FactoryResetService(PsychicHttpServer *server, FS *fs) : _server(server), fs(fs) {}

void FactoryResetService::begin() {
    _server->on(FACTORY_RESET_SERVICE_PATH, HTTP_POST,
                [this](PsychicRequest *request) { return handleRequest(request); });

    ESP_LOGV("FactoryResetService", "Registered POST endpoint: %s", FACTORY_RESET_SERVICE_PATH);
}

esp_err_t FactoryResetService::handleRequest(PsychicRequest *request) {
    request->reply(200);
    factoryReset();

    return ESP_OK;
}

/**
 * Delete function assumes that all files are stored flat, within the config directory.
 */
void FactoryResetService::factoryReset() {
    File root = fs->open(FS_CONFIG_DIRECTORY);
    File file;
    while (file = root.openNextFile()) {
        String path = file.path();
        file.close();
        fs->remove(path);
    }
    RestartService::restartNow();
}
