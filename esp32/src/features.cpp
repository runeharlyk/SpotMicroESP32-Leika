#include <features.h>

namespace feature_service {

void printFeatureConfiguration() {
    ESP_LOGI("Features", "====================== FEATURE FLAGS ======================");
    ESP_LOGI("Features", "Firmware version: %s, name: %s, target: %s", APP_VERSION, APP_NAME, BUILD_TARGET);

    ESP_LOGI("Features", "USE_CAMERA: %s", USE_CAMERA ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_MOTION: %s", USE_MOTION ? "enabled" : "disabled");

    ESP_LOGI("Features", "USE_BNO055: %s", USE_BNO055 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_MPU6050: %s", USE_MPU6050 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_HMC5883: %s", USE_HMC5883 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_BMP180: %s", USE_BMP180 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_USS: %s", USE_USS ? "enabled" : "disabled");

    ESP_LOGI("Features", "USE_PCA9685: %s", USE_PCA9685 ? "enabled" : "disabled");
    ESP_LOGI("Features", "USE_WS2812: %s", USE_WS2812 ? "enabled" : "disabled");

    ESP_LOGI("Features", "USE_MDNS: %s", USE_MDNS ? "enabled" : "disabled");
    ESP_LOGI("Features", "EMBED_WEBAPP: %s", EMBED_WEBAPP ? "enabled" : "disabled");
    ESP_LOGI("Features", "KINEMATICS_VARIANT: %s", KINEMATICS_VARIANT_STR);
    ESP_LOGI("Features", "==========================================================");
}

void features_request(const socket_message_FeaturesDataRequest& fd_req, socket_message_FeaturesDataResponse& fd_res) {
    fd_res.camera = USE_CAMERA ? true : false;
    fd_res.imu = (USE_MPU6050 || USE_BNO055) ? true : false;
    fd_res.mag = (USE_HMC5883 || USE_BNO055) ? true : false;
    fd_res.bmp = USE_BMP180 ? true : false;
    fd_res.sonar = USE_USS ? true : false;
    fd_res.servo = USE_PCA9685 ? true : false;
    fd_res.ws2812 = USE_WS2812 ? true : false;
    fd_res.mdns = USE_MDNS ? true : false;
    fd_res.embed_www = EMBED_WEBAPP ? true : false;
    strlcpy(fd_res.firmware_version, APP_VERSION, sizeof(fd_res.firmware_version));
    strlcpy(fd_res.firmware_name, APP_NAME, sizeof(fd_res.firmware_name));
    strlcpy(fd_res.firmware_built_target, BUILD_TARGET, sizeof(fd_res.firmware_built_target));
    strlcpy(fd_res.variant, KINEMATICS_VARIANT_STR, sizeof(fd_res.variant));
}

void features(socket_message_FeaturesDataResponse& proto) {
    proto.camera = USE_CAMERA ? true : false;
    proto.imu = (USE_MPU6050 || USE_BNO055) ? true : false;
    proto.mag = (USE_HMC5883 || USE_BNO055) ? true : false;
    proto.bmp = USE_BMP180 ? true : false;
    proto.sonar = USE_USS ? true : false;
    proto.servo = USE_PCA9685 ? true : false;
    proto.ws2812 = USE_WS2812 ? true : false;
    proto.mdns = USE_MDNS ? true : false;
    proto.embed_www = EMBED_WEBAPP ? true : false;
    strlcpy(proto.firmware_version, APP_VERSION, sizeof(proto.firmware_version));
    strlcpy(proto.firmware_name, APP_NAME, sizeof(proto.firmware_name));
    strlcpy(proto.firmware_built_target, BUILD_TARGET, sizeof(proto.firmware_built_target));
    strlcpy(proto.variant, KINEMATICS_VARIANT_STR, sizeof(proto.variant));
}

esp_err_t getFeatures(HttpRequest& request) {
    socket_message_FeaturesDataResponse proto = socket_message_FeaturesDataResponse_init_zero;
    features(proto);
    return request.replyProto(proto, socket_message_FeaturesDataResponse_fields);
}

} // namespace feature_service
