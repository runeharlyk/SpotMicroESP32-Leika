#include <peripherals/camera_service.h>
#include <communication/native_server.h>

namespace Camera {

static const char *const TAG = "CameraService";

static constexpr const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static constexpr const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static constexpr const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

SemaphoreHandle_t cameraMutex = xSemaphoreCreateMutex();

camera_fb_t *safe_camera_fb_get() {
    camera_fb_t *fb = NULL;
    if (xSemaphoreTakeRecursive(cameraMutex, portMAX_DELAY) == pdTRUE) {
        fb = esp_camera_fb_get();
        xSemaphoreGiveRecursive(cameraMutex);
    }
    return fb;
}

sensor_t *safe_sensor_get() {
    sensor_t *s = NULL;
    if (xSemaphoreTakeRecursive(cameraMutex, portMAX_DELAY) == pdTRUE) {
        s = esp_camera_sensor_get();
    }
    return s;
}

void safe_sensor_return() { xSemaphoreGiveRecursive(cameraMutex); }

CameraService::CameraService()
    : endpoint(CameraSettings::read, CameraSettings::update, this),
      _persistence(CameraSettings::read, CameraSettings::update, this, CAMERA_SETTINGS_FILE) {
    addUpdateHandler([&](const std::string &originId) { updateCamera(); }, false);
}

esp_err_t CameraService::begin() {
    _persistence.readFromFS();
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

    log_i("Initializing camera");
    esp_err_t err = esp_camera_init(&camera_config);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "Camera probe successful");
    else
        ESP_LOGE(TAG, "Camera probe failed with error 0x%x", err);

    return err;
}

esp_err_t CameraService::cameraStill(httpd_req_t *request) {
    camera_fb_t *fb = safe_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        return NativeServer::sendError(request, 500, "Camera capture failed");
    }

    httpd_resp_set_type(request, "image/jpeg");
    httpd_resp_set_hdr(request, "Content-Disposition", "inline; filename=capture.jpg");
    esp_err_t res = httpd_resp_send(request, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

esp_err_t CameraService::cameraStream(httpd_req_t *request) {
    httpd_resp_set_type(request, _STREAM_CONTENT_TYPE);

    camera_fb_t *fb = NULL;
    char part_buf[64];
    uint8_t *buf = NULL;
    size_t buf_len = 0;
    esp_err_t res = ESP_OK;

    while (res == ESP_OK) {
        fb = safe_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            break;
        }
        if (fb->format != PIXFORMAT_JPEG) {
            if (!frame2jpg(fb, 80, &buf, &buf_len)) {
                esp_camera_fb_return(fb);
                break;
            }
        } else {
            buf_len = fb->len;
            buf = fb->buf;
        }

        size_t hlen = snprintf(part_buf, 64, _STREAM_PART, buf_len);

        res = httpd_resp_send_chunk(request, part_buf, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(request, (const char *)buf, buf_len);
        if (res == ESP_OK) res = httpd_resp_send_chunk(request, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

        esp_camera_fb_return(fb);
        safe_sensor_return();
        buf = NULL;

        vTaskDelay(pdMS_TO_TICKS(30));
    }

    ESP_LOGI(TAG, "Stream ended");
    httpd_resp_send_chunk(request, NULL, 0);
    return ESP_OK;
}

void CameraService::updateCamera() {
    ESP_LOGI("CameraSettings", "Updating camera settings");
    sensor_t *s = safe_sensor_get();
    if (!s) {
        ESP_LOGE("CameraSettings", "Failed to update camera settings");
        safe_sensor_return();
        return;
    }
    s->set_pixformat(s, state().pixformat);
    s->set_framesize(s, state().framesize);
    s->set_brightness(s, state().brightness);
    s->set_contrast(s, state().contrast);
    s->set_saturation(s, state().saturation);
    s->set_sharpness(s, state().sharpness);
    s->set_denoise(s, state().denoise);
    s->set_gainceiling(s, state().gainceiling);
    s->set_quality(s, state().quality);
    s->set_colorbar(s, state().colorbar);
    s->set_awb_gain(s, state().awb_gain);
    s->set_wb_mode(s, state().wb_mode);
    s->set_aec2(s, state().aec2);
    s->set_ae_level(s, state().ae_level);
    s->set_aec_value(s, state().aec_value);
    s->set_agc_gain(s, state().agc_gain);
    s->set_bpc(s, state().bpc);
    s->set_wpc(s, state().wpc);
    s->set_special_effect(s, state().special_effect);
    s->set_raw_gma(s, state().raw_gma);
    s->set_lenc(s, state().lenc);
    s->set_hmirror(s, state().hmirror);
    s->set_vflip(s, state().vflip);
    s->set_dcw(s, state().dcw);
    safe_sensor_return();
}

} // namespace Camera
