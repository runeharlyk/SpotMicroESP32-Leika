#pragma once

#include <ArduinoJson.h>
#include <esp_http_server.h>
#include <WiFi.h>

#include <features.h>
#include <template/stateful_persistence.h>
#include <template/stateful_endpoint.h>

#include <settings/camera_settings.h>

namespace Camera {

#include <esp_camera.h>

#if USE_CAMERA
#include <peripherals/camera_pins.h>
#endif

#define PART_BOUNDARY "frame"

camera_fb_t *safe_camera_fb_get();
sensor_t *safe_sensor_get();
void safe_sensor_return();

class CameraService : public StatefulService<CameraSettings> {
  public:
    CameraService();

    esp_err_t begin();

    esp_err_t cameraStill(httpd_req_t *request);
    esp_err_t cameraStream(httpd_req_t *request);

    StatefulHttpEndpoint<CameraSettings> endpoint;

  private:
    FSPersistence<CameraSettings> _persistence;
    void updateCamera();
};
} // namespace Camera
