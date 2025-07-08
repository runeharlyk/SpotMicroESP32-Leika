#include <features.h>

namespace feature_service {

// New function to print all feature flags to log
void printFeatureConfiguration() {
    ESP_LOGI("Features", "====================== FEATURE FLAGS ======================");
    ESP_LOGI("Features", "Firmware version: %s, name: %s, target: %s", APP_VERSION, APP_NAME, BUILD_TARGET);

    // Core features
    ESP_LOGI("Features", "USE_UPLOAD_FIRMWARE: %s", USE_UPLOAD_FIRMWARE ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_DOWNLOAD_FIRMWARE: %s", USE_DOWNLOAD_FIRMWARE ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_SLEEP: %s", USE_SLEEP ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_CAMERA: %s", USE_CAMERA ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_MOTION: %s", USE_MOTION ? "enabled" : "disabled");

    // Sensors
    ESP_LOGI("Features", "USE_BNO055: %s", USE_BNO055 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_MPU6050: %s", USE_MPU6050 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_HMC5883: %s", USE_HMC5883 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_BMP180: %s", USE_BMP180 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_USS: %s", USE_USS ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_GPS: %s", USE_GPS ? "enabled" : "disabled");

    // Peripherals
    ESP_LOGI("Features", "USE_PCA9685: %s", USE_PCA9685 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_WS2812: %s", USE_WS2812 ? "enabled" : "disabled");

    // Web services
    ESP_LOGI("Features", "USE_MDNS: %s", USE_MDNS ? "enabled" : "disabled");
    ESP_LOGI("Features", "EMBED_WWW: %s", EMBED_WWW ? "enabled" : "disabled");
    ESP_LOGI("Features", "ENABLE_CORS: %s", ENABLE_CORS ? "enabled" : "disabled");
    ESP_LOGI("Features", "SERVE_CONFIG_FILES: %s", SERVE_CONFIG_FILES ? "enabled" : "disabled");
    ESP_LOGI("Features", "==========================================================");
}

void features(JsonObject &root) {
    root["upload_firmware"] = USE_UPLOAD_FIRMWARE;
    root["download_firmware"] = USE_DOWNLOAD_FIRMWARE;
    root["sleep"] = USE_SLEEP;
    root["camera"] = USE_CAMERA;
    root["imu"] = USE_MPU6050 || USE_BNO055;
    root["mag"] = USE_HMC5883 || USE_BNO055;
    root["bmp"] = USE_BMP180;
    root["sonar"] = USE_USS;
    root["motion"] = USE_MOTION;
    root["servo"] = USE_PCA9685;
    root["ws2812"] = USE_WS2812;
    root["mdns"] = USE_MDNS;
    root["embed_www"] = EMBED_WWW;
    root["enable_cors"] = ENABLE_CORS;
    root["serve_config_files"] = SERVE_CONFIG_FILES;
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