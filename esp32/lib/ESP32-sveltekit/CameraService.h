#ifndef CameraService_h
#define CameraService_h

#include <ArduinoJson.h>
#include <PsychicHttp.h>
#include <task_manager.h>
#include <WiFi.h>
#include <async_worker.h>
#include <Features.h>

namespace Camera {

#include <esp_camera.h>

#if USE_CAMERA
#include <CameraPins.h>
#endif

#define STREAM_SERVICE_PATH "/api/camera/stream"
#define STILL_SERVICE_PATH "/api/camera/still"

#define PART_BOUNDARY "frame"

camera_fb_t *safe_camera_fb_get();
sensor_t *safe_sensor_get();
void safe_sensor_return();

class CameraService {
  public:
    CameraService(PsychicHttpServer *server);

    void begin();

  private:
    PsychicHttpServer *_server;
    esp_err_t cameraStill(PsychicRequest *request);
    esp_err_t cameraStream(PsychicRequest *request);
    esp_err_t InitializeCamera();
};
} // namespace Camera

#endif // end CameraService_h
