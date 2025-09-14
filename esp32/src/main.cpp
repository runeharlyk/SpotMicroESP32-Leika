#include <Arduino.h>
#include <PsychicHttp.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Wire.h>

#include <filesystem.h>
#include <peripherals/peripherals.h>
#include <peripherals/servo_controller.h>
#include <peripherals/led_service.h>
#include <peripherals/camera_service.h>
#include <event_socket.h>
#include <features.h>
#include <motion.h>
#include <wifi_service.h>
#include <ap_service.h>
#include <mdns_service.h>
#include <system_service.h>

#include <www_mount.hpp>

// Communication
PsychicHttpServer server;
EventSocket socket {server, "/api/ws"};

// Core
Peripherals peripherals;
ServoController servoController;
MotionService motionService;
#if FT_ENABLED(USE_WS2812)
LEDService ledService;
#endif
#if FT_ENABLED(USE_CAMERA)
Camera::CameraService cameraService;
#endif

// Service
WiFiService wifiService;
APService apService;

void setupServer() {
    server.config.max_uri_handlers = 5 + WWW_ASSETS_COUNT;
    server.maxUploadSize = 1000000; // 1 MB;
    server.listen(80);
    server.serveStatic("/api/config/", ESP_FS, "/config/");
    server.on("/api/features", feature_service::getFeatures);
#if USE_CAMERA
    server.on("/api/camera/still", HTTP_GET,
              [&](PsychicRequest *request) { return cameraService.cameraStill(request); });
    server.on("/api/camera/stream", HTTP_GET,
              [&](PsychicRequest *request) { return cameraService.cameraStream(request); });
    server.on("/api/camera/settings", HTTP_GET,
              [&](PsychicRequest *request) { return cameraService.endpoint.getState(request); });
    server.on("/api/camera/settings", HTTP_POST, [&](PsychicRequest *request, JsonVariant &json) {
        return cameraService.endpoint.handleStateUpdate(request, json);
    });
#endif
    server.on("/api/servo/config", HTTP_GET,
              [&](PsychicRequest *request) { return servoController.endpoint.getState(request); });
    server.on("/api/servo/config", HTTP_POST, [&](PsychicRequest *request, JsonVariant &json) {
        return servoController.endpoint.handleStateUpdate(request, json);
    });
#if EMBED_WEBAPP
    mountStaticAssets(server);
#endif
    server.on("/*", HTTP_OPTIONS, [](PsychicRequest *request) { // CORS handling
        PsychicResponse response(request);
        response.setCode(200);
        return response.send();
    });
    DefaultHeaders::Instance().addHeader("Server", APP_NAME);
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Accept, Content-Type, Authorization");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Max-Age", "86400");
}

#define ANGLES_EVENT "angles"
#define INPUT_EVENT "input"
#define MODE_EVENT "mode"
#define WALK_GAIT_EVENT "walk_gait"
#define EVENT_I2C_SCAN "i2cScan"
#define EVENT_SERVO_CONFIGURATION_SETTINGS "servoPWM"
#define EVENT_SERVO_STATE "servoState"

void setupEventSocket() {
    // Motion events
    socket.onEvent(INPUT_EVENT, [&](JsonVariant &root, int originId) { motionService.handleInput(root, originId); });

    socket.onEvent(MODE_EVENT, [&](JsonVariant &root, int originId) { motionService.handleMode(root, originId); });

    socket.onEvent(WALK_GAIT_EVENT,
                   [&](JsonVariant &root, int originId) { motionService.handleWalkGait(root, originId); });

    socket.onEvent(ANGLES_EVENT, [&](JsonVariant &root, int originId) { motionService.anglesEvent(root, originId); });

    // Peripherals events
    socket.onEvent(EVENT_I2C_SCAN, [&](JsonVariant &root, int originId) {
        peripherals.scanI2C();
        JsonDocument doc;
        JsonVariant results = doc.to<JsonVariant>();
        peripherals.getI2CResult(results);
        socket.emit(EVENT_I2C_SCAN, results);
    });

    // Servo controller events
    socket.onEvent(EVENT_SERVO_CONFIGURATION_SETTINGS,
                   [&](JsonVariant &root, int originId) { servoController.servoEvent(root, originId); });
    socket.onEvent(EVENT_SERVO_STATE,
                   [&](JsonVariant &root, int originId) { servoController.stateUpdate(root, originId); });

    socket.onEvent(EVENT_SERVO_STATE, [&](JsonVariant &root, int originId) {
        const bool is_active = root["active"] | false;
        is_active ? servoController.activate() : servoController.deactivate();
    });
}

void IRAM_ATTR SpotControlLoopEntry(void *) {
    ESP_LOGI("main", "Setup complete now runing tsk");
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
        motionService.isActive() ? servoController.activate() : servoController.deactivate();
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
    MDNS.begin(APP_NAME);
    MDNS.setInstanceName(APP_NAME);
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

void setup() {
    Serial.begin(115200);

    ESP_FS.begin();

    ESP_LOGI("main", "Booting robot");

    feature_service::printFeatureConfiguration();

    xTaskCreate(serviceLoopEntry, "Service task", 4096, nullptr, 2, nullptr);

    xTaskCreatePinnedToCore(SpotControlLoopEntry, "Control task", 4096, nullptr, 5, nullptr, 1);

    ESP_LOGI("main", "Finished booting");
}

void loop() { vTaskDelete(nullptr); }