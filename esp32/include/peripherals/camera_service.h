#pragma once

#include <esp_http_server.h>

#include <features.h>
#include <eventbus.hpp>

#include <settings/camera_settings.h>

class WebServer;

namespace Camera {

#define USE_DVP_CAMERA (USE_CAMERA && !CONFIG_IDF_TARGET_ESP32P4)
#define USE_CSI_CAMERA (USE_CAMERA && CONFIG_IDF_TARGET_ESP32P4)

#if USE_DVP_CAMERA
#include <esp_camera.h>
#include <peripherals/camera_pins.h>

camera_fb_t *safe_camera_fb_get();
sensor_t *safe_sensor_get();
void safe_sensor_return();
#endif

#define PART_BOUNDARY "frame"

class CameraService {
  public:
    CameraService();

    esp_err_t begin();

    esp_err_t cameraStill(httpd_req_t *request);
    esp_err_t cameraStream(httpd_req_t *request);

    void registerRoutes(WebServer &server);

#if USE_DVP_CAMERA
  private:
    CameraSettings _settings {};
    SubscriptionHandle _settingsHandle;
    void updateCamera();
#endif
};
} // namespace Camera
