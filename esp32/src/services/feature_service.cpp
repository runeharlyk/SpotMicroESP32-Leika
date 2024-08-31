#include <services/feature_service.h>

static const char *TAG = "FeatureService";

FeaturesService::FeaturesService() {}
FeaturesService::~FeaturesService() {}

void FeaturesService::features(JsonObject &root) {
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
}

esp_err_t FeaturesService::getFeatures(PsychicRequest *request) {
    ESP_LOGV(TAG, "Retrieving features");
    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject root = response.getRoot();
    features(root);

    return response.send();
}
