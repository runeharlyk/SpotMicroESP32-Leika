#pragma once

#include <ArduinoJson.h>
#include <PsychicHttp.h>
#include <task_manager.h>
#include <WiFi.h>
#include <domain/camera/camera_settings.h>
#include <domain/stateful_service_template.h>
#include <domain/stateful_service_endpoint.h>
#include <domain/stateful_service_persistence.h>
#include <async_worker.h>
#include <features.h>

namespace spot {
namespace camera {

#include <esp_camera.h>

#if USE_CAMERA
#include <services/camera/camera_pins.h>
#endif

#define PART_BOUNDARY "frame"

camera_fb_t *safe_camera_fb_get();
sensor_t *safe_sensor_get();
void safe_sensor_return();

class CameraService : public StatefulService<CameraSettings> {
  private:
    FSPersistence<CameraSettings> _fsPersistence;

  public:
    CameraService();

    esp_err_t begin();
    static esp_err_t still(PsychicRequest *request);
    static esp_err_t stream(PsychicRequest *request);
    void updateCameraSettings();

    HttpEndpoint<CameraSettings> endpoint;
};

} // namespace camera
} // namespace spot