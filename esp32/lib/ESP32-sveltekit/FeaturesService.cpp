/**
 *   ESP32 SvelteKit
 *
 *   A simple, secure and extensible framework for IoT projects for ESP32 platforms
 *   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
 *   https://github.com/theelims/ESP32-sveltekit
 *
 *   Copyright (C) 2018 - 2023 rjwats
 *   Copyright (C) 2023 theelims
 *   Copyright (C) 2024 runeharlyk
 *
 *   All Rights Reserved. This software may be modified and distributed under
 *   the terms of the LGPL v3 license. See the LICENSE file for details.
 **/

#include <FeaturesService.h>

FeaturesService::FeaturesService(PsychicHttpServer *server) : _server(server) {}

void FeaturesService::begin() {
    _server->on(FEATURES_SERVICE_PATH, HTTP_GET, [&](PsychicRequest *request) {
        PsychicJsonResponse response = PsychicJsonResponse(request, false);
        JsonObject root = response.getRoot();

        root["security"] = USE_SECURITY;
        root["ntp"] = USE_NTP;
        root["upload_firmware"] = USE_UPLOAD_FIRMWARE;
        root["download_firmware"] = USE_DOWNLOAD_FIRMWARE;
        root["sleep"] = USE_SLEEP;
        root["battery"] = USE_BATTERY;
        root["analytics"] = USE_ANALYTICS;
        root["camera"] = USE_CAMERA;
        root["imu"] = USE_IMU;
        root["mag"] = USE_MAG;
        root["bmp"] = USE_BMP;
        root["sonar"] = USE_USS;
        root["firmware_version"] = APP_VERSION;
        root["firmware_name"] = APP_NAME;
        root["firmware_built_target"] = BUILD_TARGET;

        // Iterate over user features
        for (auto &element : userFeatures) {
            root[element.feature.c_str()] = element.enabled;
        }

        return response.send();
    });

    ESP_LOGV("FeaturesService", "Registered GET endpoint: %s", FEATURES_SERVICE_PATH);
}

void FeaturesService::addFeature(String feature, bool enabled) {
    UserFeature newFeature;
    newFeature.feature = feature;
    newFeature.enabled = enabled;

    userFeatures.push_back(newFeature);
}