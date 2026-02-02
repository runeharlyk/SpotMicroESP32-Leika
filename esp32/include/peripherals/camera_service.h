#pragma once

#include <esp_http_server.h>

#include <features.h>
#include <template/stateful_service.h>
#include <template/stateful_proto_endpoint.h>
#include <template/stateful_persistence.h>

#include <settings/camera_settings.h>

namespace Camera {

#if USE_CAMERA && !CONFIG_IDF_TARGET_ESP32P4
#include <esp_camera.h>
#include <peripherals/camera_pins.h>
#else
typedef struct {
    uint8_t *buf;
    size_t len;
} camera_fb_t;
typedef struct {
} sensor_t;
#endif

#define PART_BOUNDARY "frame"

camera_fb_t *safe_camera_fb_get();
sensor_t *safe_sensor_get();
void safe_sensor_return();

class CameraService
#if USE_CAMERA && !CONFIG_IDF_TARGET_ESP32P4
    : public StatefulService<CameraSettings>
#endif
{
  public:
    CameraService();

    esp_err_t begin();

    esp_err_t cameraStill(httpd_req_t *request);
    esp_err_t cameraStream(httpd_req_t *request);

#if USE_CAMERA && !CONFIG_IDF_TARGET_ESP32P4
    StatefulProtoEndpoint<CameraSettings, api_CameraSettings> protoEndpoint;

  private:
    FSPersistencePB<CameraSettings> _persistence;
    void updateCamera();
#endif
};
} // namespace Camera
