#include <peripherals/camera_service.h>
#include <communication/webserver.h>
#include <esp_heap_caps.h>

namespace Camera {

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

CameraService::CameraService() {}

esp_err_t CameraService::begin() {
    _settingsHandle = EventBus::subscribe<api_CameraSettings>(
        [this](const api_CameraSettings &settings) { onSettingsChanged(settings); });

    api_CameraSettings initialSettings;
    if (EventBus::peek(initialSettings)) {
        onSettingsChanged(initialSettings);
    }
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

    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
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

esp_err_t CameraService::cameraStill(httpd_req_t *request) {
    camera_fb_t *fb = safe_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        return WebServer::sendError(request, 500, "Camera capture failed");
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

void CameraService::onSettingsChanged(const api_CameraSettings &newSettings) {
    _settings = newSettings;
    updateCamera();
}

void CameraService::updateCamera() {
    ESP_LOGI(TAG, "Updating camera settings");
    sensor_t *s = safe_sensor_get();
    if (!s) {
        ESP_LOGE(TAG, "Failed to update camera settings");
        safe_sensor_return();
        return;
    }
    s->set_pixformat(s, static_cast<pixformat_t>(_settings.pixformat));
    s->set_framesize(s, static_cast<framesize_t>(_settings.framesize));
    s->set_brightness(s, _settings.brightness);
    s->set_contrast(s, _settings.contrast);
    s->set_saturation(s, _settings.saturation);
    s->set_sharpness(s, _settings.sharpness);
    s->set_denoise(s, _settings.denoise);
    s->set_gainceiling(s, static_cast<gainceiling_t>(_settings.gainceiling));
    s->set_quality(s, _settings.quality);
    s->set_colorbar(s, _settings.colorbar);
    s->set_awb_gain(s, _settings.awb_gain);
    s->set_wb_mode(s, _settings.wb_mode);
    s->set_aec2(s, _settings.aec2);
    s->set_ae_level(s, _settings.ae_level);
    s->set_aec_value(s, _settings.aec_value);
    s->set_agc_gain(s, _settings.agc_gain);
    s->set_bpc(s, _settings.bpc);
    s->set_wpc(s, _settings.wpc);
    s->set_special_effect(s, _settings.special_effect);
    s->set_raw_gma(s, _settings.raw_gma);
    s->set_lenc(s, _settings.lenc);
    s->set_hmirror(s, _settings.hmirror);
    s->set_vflip(s, _settings.vflip);
    s->set_dcw(s, _settings.dcw);
    safe_sensor_return();
}

esp_err_t CameraService::getSettings(httpd_req_t *request) {
    api_Response response = api_Response_init_zero;
    response.status_code = 200;
    response.which_payload = api_Response_camera_settings_tag;
    response.payload.camera_settings = _settings;
    return WebServer::send(request, 200, response, api_Response_fields);
}

esp_err_t CameraService::updateSettings(httpd_req_t *request, api_Request *protoReq) {
    if (protoReq->which_payload != api_Request_camera_settings_tag) {
        return ESP_FAIL;
    }

    EventBus::publish(protoReq->payload.camera_settings, "HTTPEndpoint");

    api_Response response = api_Response_init_zero;
    response.status_code = 200;
    response.which_payload = api_Response_empty_message_tag;
    return WebServer::send(request, 200, response, api_Response_fields);
}

} // namespace Camera
