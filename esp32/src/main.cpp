#include <esp_http_server.h>
#include <esp_log.h>
#include <driver/uart.h>

#include <filesystem.h>
#include <peripherals/peripherals.h>
#include <peripherals/servo_controller.h>
#include <peripherals/led_service.h>
#include <peripherals/camera_service.h>
#include <communication/websocket_adapter.h>
#include <features.h>
#include <motion.h>
#include <wifi_service.h>
#include <ap_service.h>
#include <mdns_service.h>
#include <system_service.h>
#include <utils/http_utils.h>

#include <www_mount.hpp>

httpd_handle_t server = NULL;
Websocket socket(&server, "/api/ws");

Peripherals peripherals;
ServoController servoController;
MotionService motionService;
#if FT_ENABLED(USE_WS2812)
LEDService ledService;
#endif
#if FT_ENABLED(USE_CAMERA)
Camera::CameraService cameraService;
#endif

WiFiService wifiService;
APService apService;

#define ANGLES_EVENT "angles"
#define INPUT_EVENT "input"
#define MODE_EVENT "mode"
#define WALK_GAIT_EVENT "walk_gait"
#define EVENT_I2C_SCAN "i2cScan"
#define EVENT_SERVO_CONFIGURATION_SETTINGS "servoPWM"
#define EVENT_SERVO_STATE "servoState"

esp_err_t cors_options_handler(httpd_req_t *req) { return http_utils::handle_options_cors(req); }

esp_err_t servo_config_get_handler(httpd_req_t *req) { return servoController.endpoint.getState(req); }

esp_err_t servo_config_post_handler(httpd_req_t *req) {
    JsonDocument doc;
    if (http_utils::parse_json_body(req, doc) != ESP_OK) {
        return http_utils::send_error(req, 400, "Invalid JSON");
    }
    JsonVariant json = doc.as<JsonVariant>();
    return servoController.endpoint.handleStateUpdate(req, json);
}

#if USE_CAMERA
esp_err_t camera_still_handler(httpd_req_t *req) { return cameraService.cameraStill(req); }

esp_err_t camera_stream_handler(httpd_req_t *req) { return cameraService.cameraStream(req); }

esp_err_t camera_settings_get_handler(httpd_req_t *req) { return cameraService.endpoint.getState(req); }

esp_err_t camera_settings_post_handler(httpd_req_t *req) {
    JsonDocument doc;
    if (http_utils::parse_json_body(req, doc) != ESP_OK) {
        return http_utils::send_error(req, 400, "Invalid JSON");
    }
    JsonVariant json = doc.as<JsonVariant>();
    return cameraService.endpoint.handleStateUpdate(req, json);
}
#endif

void setupServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20 + WWW_ASSETS_COUNT;
    config.stack_size = 8192;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI("main", "HTTP server started");

        httpd_uri_t config_static = {.uri = "/api/config/*",
                                     .method = HTTP_GET,
                                     .handler = [](httpd_req_t *req) -> esp_err_t {
                                         return http_utils::send_error(req, 501,
                                                                       "Static file serving not yet implemented");
                                     },
                                     .user_ctx = nullptr};
        httpd_register_uri_handler(server, &config_static);

        httpd_uri_t features_uri = {
            .uri = "/api/features", .method = HTTP_GET, .handler = feature_service::getFeatures, .user_ctx = nullptr};
        httpd_register_uri_handler(server, &features_uri);

#if USE_CAMERA
        httpd_uri_t camera_still_uri = {
            .uri = "/api/camera/still", .method = HTTP_GET, .handler = camera_still_handler, .user_ctx = nullptr};
        httpd_register_uri_handler(server, &camera_still_uri);

        httpd_uri_t camera_stream_uri = {
            .uri = "/api/camera/stream", .method = HTTP_GET, .handler = camera_stream_handler, .user_ctx = nullptr};
        httpd_register_uri_handler(server, &camera_stream_uri);

        httpd_uri_t camera_settings_get_uri = {.uri = "/api/camera/settings",
                                               .method = HTTP_GET,
                                               .handler = camera_settings_get_handler,
                                               .user_ctx = nullptr};
        httpd_register_uri_handler(server, &camera_settings_get_uri);

        httpd_uri_t camera_settings_post_uri = {.uri = "/api/camera/settings",
                                                .method = HTTP_POST,
                                                .handler = camera_settings_post_handler,
                                                .user_ctx = nullptr};
        httpd_register_uri_handler(server, &camera_settings_post_uri);
#endif

        httpd_uri_t servo_config_get_uri = {
            .uri = "/api/servo/config", .method = HTTP_GET, .handler = servo_config_get_handler, .user_ctx = nullptr};
        httpd_register_uri_handler(server, &servo_config_get_uri);

        httpd_uri_t servo_config_post_uri = {
            .uri = "/api/servo/config", .method = HTTP_POST, .handler = servo_config_post_handler, .user_ctx = nullptr};
        httpd_register_uri_handler(server, &servo_config_post_uri);

#if EMBED_WEBAPP
        mountStaticAssets(server);
#endif

        httpd_uri_t options_uri = {
            .uri = "/*", .method = HTTP_OPTIONS, .handler = cors_options_handler, .user_ctx = nullptr};
        httpd_register_uri_handler(server, &options_uri);

    } else {
        ESP_LOGE("main", "Failed to start HTTP server");
    }
}

void setupEventSocket() {
    socket.onEvent(INPUT_EVENT, [&](JsonVariant &root, int originId) { motionService.handleInput(root, originId); });

    socket.onEvent(MODE_EVENT, [&](JsonVariant &root, int originId) {
        servoController.setMode(SERVO_CONTROL_STATE::ANGLE);
        motionService.handleMode(root, originId);
        motionService.isActive() ? servoController.activate() : servoController.deactivate();
    });

    socket.onEvent(WALK_GAIT_EVENT,
                   [&](JsonVariant &root, int originId) { motionService.handleWalkGait(root, originId); });

    socket.onEvent(ANGLES_EVENT, [&](JsonVariant &root, int originId) { motionService.anglesEvent(root, originId); });

    socket.onEvent(EVENT_I2C_SCAN, [&](JsonVariant &root, int originId) {
        peripherals.scanI2C();
        JsonDocument doc;
        JsonVariant results = doc.to<JsonVariant>();
        peripherals.getI2CResult(results);
        socket.emit(EVENT_I2C_SCAN, results);
    });

    socket.onEvent(EVENT_SERVO_CONFIGURATION_SETTINGS,
                   [&](JsonVariant &root, int originId) { servoController.servoEvent(root, originId); });

    socket.onEvent(EVENT_SERVO_STATE,
                   [&](JsonVariant &root, int originId) { servoController.stateUpdate(root, originId); });
}

void IRAM_ATTR SpotControlLoopEntry(void *) {
    ESP_LOGI("main", "Setup complete now running control task");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = 5 / portTICK_PERIOD_MS;

    peripherals.begin();
    servoController.begin();
    motionService.begin();

    for (;;) {
        CALLS_PER_SECOND(SpotControlLoopEntry);
        peripherals.update();
        motionService.update(&peripherals);
        servoController.setAngles(motionService.getAngles());
        servoController.update();
#if FT_ENABLED(USE_WS2812)
        ledService.loop();
#endif
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void IRAM_ATTR serviceLoopEntry(void *) {
    ESP_LOGI("main", "Service control task starting");

    wifiService.begin();
    mdns_service::begin(APP_NAME);
    apService.begin();

    setupServer();

    socket.begin();
    setupEventSocket();

    ESP_LOGI("main", "Service control task started");
    for (;;) {
        wifiService.loop();
        apService.loop();
        EXECUTE_EVERY_N_MS(2000, system_service::emitMetrics());

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

extern "C" void app_main() {
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_FS.begin();

    ESP_LOGI("main", "Booting robot");

    feature_service::printFeatureConfiguration();

    xTaskCreate(serviceLoopEntry, "Service task", 8192, nullptr, 2, nullptr);

    xTaskCreatePinnedToCore(SpotControlLoopEntry, "Control task", 8192, nullptr, 5, nullptr, 1);

    ESP_LOGI("main", "Finished booting");
}
