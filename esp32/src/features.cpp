#include <features.h>
#include <communication/webserver.h>
#include <settings/placeholders.h>
#include <esp_app_desc.h>

namespace feature_service {

// The app reads the env out of an image file behind this marker, so the build target is only ever taken from here.
static const char BUILD_TARGET_MARKER[] = "LEIKA_ENV=" BUILD_TARGET;

const char *buildTarget() { return BUILD_TARGET_MARKER + sizeof("LEIKA_ENV=") - 1; }

void printFeatureConfiguration() {
    ESP_LOGI("Features", "Firmware version: %s, name: %s, target: %s, camera: %s", APP_VERSION, APP_NAME, buildTarget(),
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
    fd_res.firmware_built_target = const_cast<char *>(buildTarget());
    esp_app_get_elf_sha256(fd_res.firmware_elf_sha256, sizeof(fd_res.firmware_elf_sha256));
    fd_res.variant = const_cast<char *>(variantName(features.variant));
    fd_res.device_id = const_cast<char *>(deviceId().c_str());
    strncpy(fd_res.robot_name, features.robotName, sizeof(fd_res.robot_name) - 1);
    strncpy(fd_res.hostname, features.hostname, sizeof(fd_res.hostname) - 1);
}

} // namespace feature_service
