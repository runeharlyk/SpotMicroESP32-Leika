#ifndef CameraService_h
#define CameraService_h

#include <ArduinoJson.h>
#include <PsychicHttp.h>
#include <SecurityManager.h>
#include <TaskManager.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <async_worker.h>

#define CAMERA_MODEL_AI_THINKER
#include <CameraPins.h>

#define STREAM_SERVICE_PATH "/api/camera/stream"
#define STILL_SERVICE_PATH "/api/camera/still"

#define PART_BOUNDARY "frame"

camera_fb_t *safe_camera_fb_get();
sensor_t* safe_sensor_get();
void safe_sensor_return();

class CameraService
{
  public:
    CameraService(PsychicHttpServer *server, TaskManager *taskManager, SecurityManager *securityManager);

    void begin();

  private:
    PsychicHttpServer *_server;
    TaskManager *_taskManager;
    SecurityManager *_securityManager;
    esp_err_t cameraStill(PsychicRequest *request);
    esp_err_t cameraStream(PsychicRequest *request);
    esp_err_t InitializeCamera();
};

#endif // end CameraService_h
