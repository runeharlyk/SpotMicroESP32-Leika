#include <peripherals/camera_service.h>

namespace Camera {

static const char* const TAG = "CameraService";

static constexpr const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static constexpr const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static constexpr const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

SemaphoreHandle_t cameraMutex = xSemaphoreCreateMutex();

camera_fb_t* safe_camera_fb_get() {
    camera_fb_t* fb = NULL;
    if (xSemaphoreTakeRecursive(cameraMutex, portMAX_DELAY) == pdTRUE) {
        fb = esp_camera_fb_get();
        xSemaphoreGiveRecursive(cameraMutex);
    }
    return fb;
}

sensor_t* safe_sensor_get() {
    sensor_t* s = NULL;
    if (xSemaphoreTakeRecursive(cameraMutex, portMAX_DELAY) == pdTRUE) {
        s = esp_camera_sensor_get();
    }
    return s;
}

void safe_sensor_return() { xSemaphoreGiveRecursive(cameraMutex); }

CameraService::CameraService()
    : endpoint(CameraSettings::read, CameraSettings::update, this, socket_message_CameraSettingsData_fields),
      _persistence(CameraSettings::read, CameraSettings::update, this, CAMERA_SETTINGS_FILE,
                   socket_message_CameraSettingsData_fields) {
    addUpdateHandler([&](const std::string& originId) { updateCamera(); }, false);
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

esp_err_t CameraService::cameraStill(HttpRequest& request) {
    camera_fb_t* fb = safe_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        return request.reply(500);
    }

    httpd_resp_set_type(request.raw, "image/jpeg");
    httpd_resp_set_hdr(request.raw, "Content-Disposition", "inline; filename=capture.jpg");
    esp_err_t ret = httpd_resp_send(request.raw, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return ret;
}

struct StreamContext {
    httpd_handle_t hd;
    int fd;
};

void streamTask(void* pv) {
    StreamContext* ctx = static_cast<StreamContext*>(pv);

    httpd_ws_frame_t ws_pkt = {};
    char part_buf[64];

    for (;;) {
        camera_fb_t* fb = safe_camera_fb_get();
        if (!fb) {
            ESP_LOGE("Stream", "Camera capture failed");
            break;
        }

        size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, fb->len);

        httpd_resp_send_chunk(nullptr, part_buf, hlen);
        httpd_resp_send_chunk(nullptr, (const char*)fb->buf, fb->len);
        httpd_resp_send_chunk(nullptr, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

        esp_camera_fb_return(fb);
        safe_sensor_return();

        taskYIELD();
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    delete ctx;
    vTaskDelete(NULL);
}

esp_err_t CameraService::cameraStream(HttpRequest& request) {
    httpd_resp_set_type(request.raw, _STREAM_CONTENT_TYPE);

    esp_err_t ret = httpd_resp_send_chunk(request.raw, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    if (ret != ESP_OK) {
        return ret;
    }

    camera_fb_t* fb = NULL;
    char part_buf[64];

    while (true) {
        fb = safe_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            break;
        }

        size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, fb->len);

        ret = httpd_resp_send_chunk(request.raw, part_buf, hlen);
        if (ret != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }

        ret = httpd_resp_send_chunk(request.raw, (const char*)fb->buf, fb->len);
        if (ret != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }

        ret = httpd_resp_send_chunk(request.raw, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        esp_camera_fb_return(fb);

        if (ret != ESP_OK) break;

        vTaskDelay(pdMS_TO_TICKS(30));
    }

    return httpd_resp_send_chunk(request.raw, NULL, 0);
}

void CameraService::updateCamera() {
    ESP_LOGI("CameraSettings", "Updating camera settings");
    sensor_t* s = safe_sensor_get();
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
