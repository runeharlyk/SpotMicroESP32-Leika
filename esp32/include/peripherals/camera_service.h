#pragma once

#include <esp_http_server.h>

#include <features.h>
#include <event_bus/event_bus.h>

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

class CameraService {
  public:
    CameraService();

    esp_err_t begin();

    esp_err_t cameraStill(httpd_req_t *request);
    esp_err_t cameraStream(httpd_req_t *request);

    esp_err_t getSettings(httpd_req_t *request);
    esp_err_t updateSettings(httpd_req_t *request, api_Request *protoReq);

  private:
    static constexpr const char *TAG = "CameraService";

    void onSettingsChanged(const api_CameraSettings &newSettings);
    void updateCamera();

    CameraSettings _settings = CameraSettings_defaults();
    EventBus::Handle<api_CameraSettings> _settingsHandle;
};
} // namespace Camera
