#ifndef CameraService_h
#define CameraService_h

#include <ArduinoJson.h>
#include <PsychicHttp.h>
#include <task_manager.h>
#include <WiFi.h>
#include <async_worker.h>
#include <features.h>

namespace Camera {

#include <esp_camera.h>

#if USE_CAMERA
#include <camera_pins.h>
#endif

#define PART_BOUNDARY "frame"

camera_fb_t *safe_camera_fb_get();
sensor_t *safe_sensor_get();
void safe_sensor_return();

class CameraService {
  public:
    CameraService();

    esp_err_t begin();

    esp_err_t cameraStill(PsychicRequest *request);
    esp_err_t cameraStream(PsychicRequest *request);

  private:
    PsychicHttpServer *_server;
};
} // namespace Camera

#endif // end CameraService_h
