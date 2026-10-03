#include <features.h>
#include <communication/webserver.h>
#include <settings/placeholders.h>

namespace feature_service {

void printFeatureConfiguration() {
    ESP_LOGI("Features", "Firmware version: %s, name: %s, target: %s, camera: %s", APP_VERSION, APP_NAME, BUILD_TARGET,
             USE_CAMERA ? "built in" : "none");
}

void features_request(const RuntimeFeatures &features, socket_message_FeaturesDataResponse &fd_res) {
    const SensorStatus &sensors = features.sensors;
    fd_res.camera = features.cameraActive;
    fd_res.camera_detected = features.cameraDetected;
    fd_res.imu = sensors.imuActive;
    fd_res.imu_detected = sensors.imuDetected;
    fd_res.mag = sensors.magActive;
    fd_res.mag_detected = sensors.magDetected;
    fd_res.bmp = sensors.bmpActive;
    fd_res.bmp_detected = sensors.bmpDetected;
    fd_res.gesture = sensors.gestureActive;
    fd_res.gesture_detected = sensors.gestureDetected;
    fd_res.servo = features.servoDetected;
    fd_res.servo_detected = features.servoDetected;
    fd_res.ws2812 = features.ws2812;
    fd_res.mdns = true;
    fd_res.embed_www = true;
    fd_res.firmware_version = const_cast<char *>(APP_VERSION);
    fd_res.firmware_name = const_cast<char *>(APP_NAME);
    fd_res.firmware_built_target = const_cast<char *>(BUILD_TARGET);
    fd_res.variant = const_cast<char *>(variantName(features.variant));
    fd_res.device_id = const_cast<char *>(deviceId().c_str());
    strncpy(fd_res.robot_name, features.robotName, sizeof(fd_res.robot_name) - 1);
    strncpy(fd_res.hostname, features.hostname, sizeof(fd_res.hostname) - 1);
}

} // namespace feature_service
