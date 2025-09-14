#include <features.h>

namespace feature_service {

// New function to print all feature flags to log
void printFeatureConfiguration() {
    ESP_LOGI("Features", "====================== FEATURE FLAGS ======================");
    ESP_LOGI("Features", "Firmware version: %s, name: %s, target: %s", APP_VERSION, APP_NAME, BUILD_TARGET);

    // Core features
    ESP_LOGI("Features", "USE_CAMERA: %s", USE_CAMERA ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_MOTION: %s", USE_MOTION ? "enabled" : "disabled");

    // Sensors
    ESP_LOGI("Features", "USE_BNO055: %s", USE_BNO055 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_MPU6050: %s", USE_MPU6050 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_HMC5883: %s", USE_HMC5883 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_BMP180: %s", USE_BMP180 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_USS: %s", USE_USS ? "enabled" : "disabled");

    // Peripherals
    ESP_LOGI("Features", "USE_PCA9685: %s", USE_PCA9685 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_WS2812: %s", USE_WS2812 ? "enabled" : "disabled");

    // Web services
    ESP_LOGI("Features", "USE_MDNS: %s", USE_MDNS ? "enabled" : "disabled");
    ESP_LOGI("Features", "EMBED_WEBAPP: %s", EMBED_WEBAPP ? "enabled" : "disabled");
    ESP_LOGI("Features", "KINEMATICS_VARIANT: %s", KINEMATICS_VARIANT_STR);
    ESP_LOGI("Features", "==========================================================");
}

void features(JsonObject &root) {
    root["camera"] = USE_CAMERA ? true : false;
    root["imu"] = (USE_MPU6050 || USE_BNO055) ? true : false;
    root["mag"] = (USE_HMC5883 || USE_BNO055) ? true : false;
    root["bmp"] = USE_BMP180 ? true : false;
    root["sonar"] = USE_USS ? true : false;
    root["servo"] = USE_PCA9685 ? true : false;
    root["ws2812"] = USE_WS2812 ? true : false;
    root["mdns"] = USE_MDNS ? true : false;
    root["embed_www"] = EMBED_WEBAPP ? true : false;
    root["firmware_version"] = APP_VERSION;
    root["firmware_name"] = APP_NAME;
    root["firmware_built_target"] = BUILD_TARGET;
    root["variant"] = KINEMATICS_VARIANT_STR;
}

esp_err_t getFeatures(PsychicRequest *request) {
    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject root = response.getRoot();
    features(root);
    return response.send();
}

} // namespace feature_service