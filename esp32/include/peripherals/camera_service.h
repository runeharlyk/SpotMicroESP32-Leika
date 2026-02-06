#pragma once

#include <esp_http_server.h>

#include <features.h>
#include <template/stateful_service.h>
#include <template/stateful_proto_endpoint.h>
#include <template/stateful_persistence.h>

#include <settings/camera_settings.h>

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

class CameraService
#if USE_DVP_CAMERA
    : public StatefulService<CameraSettings>
#endif
{
  public:
    CameraService();

    esp_err_t begin();

    esp_err_t cameraStill(httpd_req_t *request);
    esp_err_t cameraStream(httpd_req_t *request);

#if USE_DVP_CAMERA
    StatefulProtoEndpoint<CameraSettings, api_CameraSettings> protoEndpoint;

  private:
    FSPersistencePB<CameraSettings> _persistence;
    void updateCamera();
#endif
};
} // namespace Camera
