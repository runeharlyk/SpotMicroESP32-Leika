#include <features.h>

namespace feature_service {

void features(JsonObject &root) {
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

esp_err_t getFeatures(PsychicRequest *request) {
    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject root = response.getRoot();
    features(root);
    return response.send();
}

} // namespace feature_service