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

        root["security"] = FT_SECURITY;
        root["ntp"] = FT_NTP;
        root["upload_firmware"] = FT_UPLOAD_FIRMWARE;
        root["download_firmware"] = FT_DOWNLOAD_FIRMWARE;
        root["sleep"] = FT_SLEEP;
        root["battery"] = FT_BATTERY;
        root["analytics"] = FT_ANALYTICS;
        root["camera"] = FT_CAMERA;
        root["imu"] = FT_IMU;
        root["mag"] = FT_MAG;
        root["bmp"] = FT_BMP;
        root["sonar"] = FT_USS;
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