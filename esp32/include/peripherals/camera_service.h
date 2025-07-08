#ifndef CameraService_h
#define CameraService_h

#include <ArduinoJson.h>
#include <PsychicHttp.h>
#include <WiFi.h>
#include <async_worker.h>

#include <features.h>
#include <task_manager.h>
#include <template/stateful_socket.h>
#include <template/stateful_persistence.h>
#include <template/stateful_endpoint.h>

#include <settings/camera_settings.h>

namespace Camera {

#include <esp_camera.h>

#if USE_CAMERA
#include <peripherals/camera_pins.h>
#endif

#define PART_BOUNDARY "frame"

#define EVENT_CAMERA_SETTINGS "CameraSettings"

camera_fb_t *safe_camera_fb_get();
sensor_t *safe_sensor_get();
void safe_sensor_return();

class CameraService : public StatefulService<CameraSettings> {
  public:
    CameraService();

    esp_err_t begin();

    esp_err_t cameraStill(PsychicRequest *request);
    esp_err_t cameraStream(PsychicRequest *request);

    StatefulHttpEndpoint<CameraSettings> endpoint;

  private:
    EventEndpoint<CameraSettings> _eventEndpoint;
    FSPersistence<CameraSettings> _persistence;
    void updateCamera();
};
} // namespace Camera

#endif // end CameraService_h
