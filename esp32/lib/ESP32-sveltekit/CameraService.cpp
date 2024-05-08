#include <CameraService.h>

static const char *_STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

SemaphoreHandle_t cameraMutex = xSemaphoreCreateMutex();

camera_fb_t *safe_camera_fb_get() {
    camera_fb_t *fb = NULL;
    if (xSemaphoreTake(cameraMutex, portMAX_DELAY) == pdTRUE) {
        fb = esp_camera_fb_get();
        xSemaphoreGive(cameraMutex);
    }
    return fb;
}

CameraService::CameraService(PsychicHttpServer *server,
                             TaskManager *taskManager,
                             SecurityManager *securityManager)
    : _server(server),
      _taskManager(taskManager),
      _securityManager(securityManager),
      _videoStream(_STREAM_CONTENT_TYPE) {}
void CameraService::begin() {
    InitializeCamera();
    _server->on(
        STILL_SERVICE_PATH, HTTP_GET,
        _securityManager->wrapRequest(
            std::bind(&CameraService::cameraStill, this, std::placeholders::_1),
            AuthenticationPredicates::IS_AUTHENTICATED)); 

    ESP_LOGV("CameraService", "Registered GET endpoint: %s", STILL_SERVICE_PATH);
}

esp_err_t CameraService::InitializeCamera() {
    camera_config_t camera_config;
    camera_config.ledc_channel = LEDC_CHANNEL_0;
    camera_config.ledc_timer = LEDC_TIMER_0;
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
    camera_config.xclk_freq_hz = 20000000;
    camera_config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound()) {
        camera_config.frame_size = FRAMESIZE_SVGA;
        camera_config.jpeg_quality = 10;
        camera_config.fb_count = 2;
    } else {
        camera_config.frame_size = FRAMESIZE_SVGA;
        camera_config.jpeg_quality = 12;
        camera_config.fb_count = 1;
    }

    log_i("Initializing camera");
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) log_e("Camera probe failed with error 0x%x", err);

    return err;
}

esp_err_t CameraService::cameraStill(PsychicRequest *request) {
    camera_fb_t *fb = safe_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        request->reply(500, "text/plain", "Camera capture failed");
        return ESP_FAIL;
    }
    PsychicStreamResponse response = PsychicStreamResponse(request, "image/jpeg", "capture.jpg");
    response.beginSend();
    response.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return response.endSend();
}
