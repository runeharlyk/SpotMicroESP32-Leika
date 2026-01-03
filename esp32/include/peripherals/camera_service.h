#pragma once

#include <communication/http_server.h>
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

camera_fb_t* safe_camera_fb_get();
sensor_t* safe_sensor_get();
void safe_sensor_return();

class CameraService : public StatefulService<CameraSettings> {
  public:
    CameraService();

    esp_err_t begin();

    esp_err_t cameraStill(HttpRequest& request);
    esp_err_t cameraStream(HttpRequest& request);

    StatefulHttpEndpoint<CameraSettings, socket_message_CameraSettingsData> endpoint;

  private:
    FSPersistence<CameraSettings, socket_message_CameraSettingsData> _persistence;
    void updateCamera();
};
} // namespace Camera
