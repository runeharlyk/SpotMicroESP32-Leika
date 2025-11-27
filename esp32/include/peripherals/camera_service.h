#ifndef CameraService_h
#define CameraService_h

#include <ArduinoJson.h>
#include <esp_http_server.h>
#include <utils/http_utils.h>
#include <async_worker.h>

#include <features.h>
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

    esp_err_t cameraStill(httpd_req_t *req);
    esp_err_t cameraStream(httpd_req_t *req);

    StatefulHttpEndpoint<CameraSettings> endpoint;

  private:
    EventEndpoint<CameraSettings> _eventEndpoint;
    FSPersistence<CameraSettings> _persistence;
    void updateCamera();
};
} // namespace Camera

#endif // end CameraService_h
