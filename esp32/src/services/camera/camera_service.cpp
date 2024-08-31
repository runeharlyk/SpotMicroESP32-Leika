#include <services/camera/camera_service.h>

namespace spot {
namespace camera {

static const char *const TAG = "CameraService";

static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

SemaphoreHandle_t cameraMutex = xSemaphoreCreateMutex();

camera_fb_t *safe_camera_fb_get() {
    camera_fb_t *fb = NULL;
    if (xSemaphoreTake(cameraMutex, portMAX_DELAY) == pdTRUE) {
        fb = esp_camera_fb_get();
        xSemaphoreGive(cameraMutex);
    }
    return fb;
}

sensor_t *safe_sensor_get() {
    sensor_t *s = NULL;
    if (xSemaphoreTake(cameraMutex, portMAX_DELAY) == pdTRUE) {
        s = esp_camera_sensor_get();
        if (!s) {
            xSemaphoreGive(cameraMutex);
        }
    }
    return s;
}

void safe_sensor_return() {
    if (uxSemaphoreGetCount(cameraMutex) == 0) {
        xSemaphoreGive(cameraMutex);
    }
}

CameraService::CameraService()
    : endpoint(CameraSettings::read, CameraSettings::update, this),
      _fsPersistence(CameraSettings::read, CameraSettings::update, this, &ESPFS, CAMERA_SETTINGS_FILE) {
    addUpdateHandler([&](const String &originId) { updateCameraSettings(); }, false);
}

esp_err_t CameraService::begin() {
    camera_config_t camera_config;
    camera_config.ledc_channel = LEDC_CHANNEL_0;
    camera_config.ledc_timer = LEDC_TIMER_0;
#if FT_ENABLED(USE_CAMERA)
    camera_config.pin_d0 = Y2_GPIO_NUM;
    camera_config.pin_d1 = Y3_GPIO_NUM;
    camera_config.pin_d2 = Y4_GPIO_NUM;
    camera_config.pin_d3 = Y5_GPIO_NUM;
    camera_config.pin_d4 = Y6_GPIO_NUM;
    camera_config.pin_d5 = Y7_GPIO_NUM;
    camera_config.pin_d6 = Y8_GPIO_NUM;
    camera_config.pin_d7 = Y9_GPIO_NUM;
    camera_config.pin_xclk = XCLK_GPIO_NUM;
    camera_config.pin_pclk = PCLK_GPIO_NUM;
    camera_config.pin_vsync = VSYNC_GPIO_NUM;
    camera_config.pin_href = HREF_GPIO_NUM;
    camera_config.pin_sccb_sda = SIOD_GPIO_NUM;
    camera_config.pin_sccb_scl = SIOC_GPIO_NUM;
    camera_config.pin_pwdn = PWDN_GPIO_NUM;
    camera_config.pin_reset = RESET_GPIO_NUM;
#endif
    camera_config.xclk_freq_hz = 20000000;
    camera_config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound()) {
        camera_config.frame_size = FRAMESIZE_SVGA;
        camera_config.jpeg_quality = 10;
        camera_config.fb_location = CAMERA_FB_IN_PSRAM;
        camera_config.fb_count = 2;
        camera_config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
        camera_config.frame_size = FRAMESIZE_SVGA;
        camera_config.jpeg_quality = 12;
        camera_config.fb_count = 1;
    }

    ESP_LOGI(TAG, "Initializing camera");
    esp_err_t err = esp_camera_init(&camera_config);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "Camera probe successful");
    else
        ESP_LOGE(TAG, "Camera probe failed with error 0x%x", err);

    return err;
}

esp_err_t CameraService::still(PsychicRequest *request) {
    camera_fb_t *fb = safe_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        request->reply(500, "text/plain", "Camera capture failed");
        return ESP_FAIL;
    }
    PsychicStreamResponse response = PsychicStreamResponse(request, "image/jpeg", "capture.jpg");
    response.beginSend();
    response.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return response.endSend();
}

void streamTask(void *pv) {
    esp_err_t res = ESP_OK;

    PsychicRequest *request = static_cast<PsychicRequest *>(pv);

    httpd_req_t *copy = nullptr;
    res = httpd_req_async_handler_begin(request->request(), &copy);
    if (res != ESP_OK) {
        return;
    }
    PsychicHttpServer *server = request->server();
    PsychicRequest new_request = PsychicRequest(server, copy);
    request = &new_request;

    PsychicStreamResponse response = PsychicStreamResponse(request, _STREAM_CONTENT_TYPE);
    camera_fb_t *fb = NULL;

    char *part_buf[64];
    size_t buf_len = 0;
    uint8_t *buf = NULL;
    int64_t fr_start = esp_timer_get_time();

    response.beginSend();

    for (;;) {
        fb = safe_camera_fb_get();
        if (!fb) {
            ESP_LOGE("Stream", "Camera capture failed");
            break;
        }
        if (fb->format != PIXFORMAT_JPEG) {
            if (!frame2jpg(fb, 80, &buf, &buf_len)) break;
        } else {
            buf_len = fb->len;
            buf = fb->buf;
        }

        size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, buf_len);
        size_t w = response.write((const char *)part_buf, hlen);
        w += response.write((const char *)buf, buf_len);
        w += response.write((char *)_STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        if (w == 62) break;
        esp_camera_fb_return(fb);
        safe_sensor_return();
        buf = NULL;
        taskYIELD();
        int64_t delay = 30000ll - esp_timer_get_time() - fr_start;
        if (delay > 0) vTaskDelay(pdMS_TO_TICKS(delay));
    }
    ESP_LOGI("Stream", "Stream ended");
    response.endSend();
    httpd_req_async_handler_complete(copy);
    vTaskDelete(NULL);
}

esp_err_t CameraService::stream(PsychicRequest *request) {
    g_task_manager.createTask(streamTask, "Stream client task", 4096, request, 4);
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

void CameraService::updateCameraSettings() {
    ESP_LOGI("CameraSettings", "Updating camera settings");
    sensor_t *s = safe_sensor_get();
    if (!s) {
        ESP_LOGE("CameraSettings", "Failed to update camera settings");
        safe_sensor_return();
        return;
    }
    s->set_pixformat(s, _state.pixformat);
    s->set_framesize(s, _state.framesize);
    s->set_brightness(s, _state.brightness);
    s->set_contrast(s, _state.contrast);
    s->set_saturation(s, _state.saturation);
    s->set_sharpness(s, _state.sharpness);
    s->set_denoise(s, _state.denoise);
    s->set_gainceiling(s, _state.gainceiling);
    s->set_quality(s, _state.quality);
    s->set_colorbar(s, _state.colorbar);
    s->set_awb_gain(s, _state.awb_gain);
    s->set_wb_mode(s, _state.wb_mode);
    s->set_aec2(s, _state.aec2);
    s->set_ae_level(s, _state.ae_level);
    s->set_aec_value(s, _state.aec_value);
    s->set_agc_gain(s, _state.agc_gain);
    s->set_bpc(s, _state.bpc);
    s->set_wpc(s, _state.wpc);
    s->set_special_effect(s, _state.special_effect);
    s->set_raw_gma(s, _state.raw_gma);
    s->set_lenc(s, _state.lenc);
    s->set_hmirror(s, _state.hmirror);
    s->set_vflip(s, _state.vflip);
    s->set_dcw(s, _state.dcw);
    safe_sensor_return();
}

} // namespace camera
} // namespace spot