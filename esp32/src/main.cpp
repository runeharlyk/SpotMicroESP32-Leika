#include <Arduino.h>
#include <PsychicHttp.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Wire.h>
#include <map>

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

#include <www_mount.hpp>

// Communication
PsychicHttpServer server;
Websocket socket {server, "/api/ws"};

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
#if FT_ENABLED(USE_MDNS)
MDNSService mdnsService;
#endif

// Service
WiFiService wifiService;
APService apService;

void setupServer() {
    server.config.max_uri_handlers = 50 + WWW_ASSETS_COUNT;
    server.maxUploadSize = 1000000; // 1 MB;
    server.listen(80);
    server.on("/api/features", feature_service::getFeatures);
    server.on("/api/system/status", HTTP_GET,
              [&](PsychicRequest *request) { return system_service::getStatus(request); });
    server.on("/api/system/reset", HTTP_POST,
              [&](PsychicRequest *request, JsonVariant &json) { return system_service::handleReset(request); });
    server.on("/api/system/restart", HTTP_POST,
              [&](PsychicRequest *request, JsonVariant &json) { return system_service::handleRestart(request); });
    server.on("/api/system/sleep", HTTP_POST,
              [&](PsychicRequest *request, JsonVariant &json) { return system_service::handleSleep(request); });
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

    // WiFi
    server.on("/api/wifi/sta/settings", HTTP_GET,
              [&](PsychicRequest *request) { return wifiService.endpoint.getState(request); });
    server.on("/api/wifi/sta/settings", HTTP_POST, [&](PsychicRequest *request, JsonVariant &json) {
        return wifiService.endpoint.handleStateUpdate(request, json);
    });
    server.on("/api/wifi/scan", HTTP_GET, [&](PsychicRequest *request) { return wifiService.handleScan(request); });
    server.on("/api/wifi/networks", HTTP_GET,
              [&](PsychicRequest *request) { return wifiService.getNetworks(request); });
    server.on("/api/wifi/sta/status", HTTP_GET,
              [&](PsychicRequest *request) { return wifiService.getNetworkStatus(request); });

    // AP
    server.on("/api/ap/status", HTTP_GET, [&](PsychicRequest *request) { return apService.getStatus(request); });
    server.on("/api/ap/settings", HTTP_GET,
              [&](PsychicRequest *request) { return apService.endpoint.getState(request); });
    server.on("/api/ap/settings", HTTP_POST, [&](PsychicRequest *request, JsonVariant &json) {
        return apService.endpoint.handleStateUpdate(request, json);
    });

    // Peripherals
    server.on("/api/peripherals", HTTP_GET,
              [&](PsychicRequest *request) { return peripherals.endpoint.getState(request); });
    server.on("/api/peripherals", HTTP_POST, [&](PsychicRequest *request, JsonVariant &json) {
        return peripherals.endpoint.handleStateUpdate(request, json);
    });

    // MDNS
#if FT_ENABLED(USE_MDNS)
    server.on("/api/mdns", HTTP_GET, [&](PsychicRequest *request) { return mdnsService.endpoint.getState(request); });
    server.on("/api/mdns", HTTP_POST, [&](PsychicRequest *request, JsonVariant &json) {
        return mdnsService.endpoint.handleStateUpdate(request, json);
    });
    server.on("/api/mdns/status", HTTP_GET, [&](PsychicRequest *request) { return mdnsService.getStatus(request); });
    server.on("/api/mdns/query", HTTP_POST,
              [&](PsychicRequest *request, JsonVariant &json) { return mdnsService.queryServices(request, json); });
#endif

    // Filesystem
    server.on("/api/config/*", HTTP_GET, [](PsychicRequest *request) { return FileSystem::getConfigFile(request); });
    server.on("/api/files", HTTP_GET, [&](PsychicRequest *request) { return FileSystem::getFiles(request); });
    server.on("/api/files", HTTP_POST, FileSystem::uploadHandler);
    server.on("/api/files/delete", HTTP_POST,
              [&](PsychicRequest *request, JsonVariant &json) { return FileSystem::handleDelete(request, json); });
    server.on("/api/files/edit", HTTP_POST,
              [&](PsychicRequest *request, JsonVariant &json) { return FileSystem::handleEdit(request, json); });
    server.on("/api/files/mkdir", HTTP_POST,
              [&](PsychicRequest *request, JsonVariant &json) { return FileSystem::mkdir(request, json); });
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

void setupEventSocket() {
    socket.on<socket_message_HumanInputData>(
        [&](const socket_message_HumanInputData &data, int clientId) { motionService.handleInput(data); });

    socket.on<socket_message_ModeData>([&](const socket_message_ModeData &data, int clientId) {
        servoController.setMode(SERVO_CONTROL_STATE::ANGLE);
        motionService.handleMode(data);
        motionService.isActive() ? servoController.activate() : servoController.deactivate();
    });

    socket.on<socket_message_WalkGaitData>(
        [&](const socket_message_WalkGaitData &data, int clientId) { motionService.handleWalkGait(data); });

    socket.on<socket_message_AnglesData>(
        [&](const socket_message_AnglesData &data, int clientId) { motionService.handleAngles(data); });

    socket.on<socket_message_ServoPWMData>([&](const socket_message_ServoPWMData &data, int clientId) {
        servoController.setServoPWM(data.servo_id, data.servo_pwm);
    });

    socket.on<socket_message_ServoStateData>([&](const socket_message_ServoStateData &data, int clientId) {
        data.active ? servoController.activate() : servoController.deactivate();
    });

    using CorrelationHandler =
        std::function<void(const socket_message_CorrelationRequest &, socket_message_CorrelationResponse &)>;
    static std::map<pb_size_t, CorrelationHandler> correlationHandlers = {
        {socket_message_CorrelationRequest_features_data_request_tag,
         [](const auto &req, auto &res) {
             res.which_response = socket_message_CorrelationResponse_features_data_response_tag;
             feature_service::features_request(req.request.features_data_request, res.response.features_data_response);
         }},
        {socket_message_CorrelationRequest_i2c_scan_data_request_tag,
         [](const auto &req, auto &res) {
             res.which_response = socket_message_CorrelationResponse_i2c_scan_data_tag;
             peripherals.scanI2C();
             peripherals.getI2CScanProto(res.response.i2c_scan_data);
         }},
        {socket_message_CorrelationRequest_imu_calibrate_execute_tag,
         [](const auto &req, auto &res) {
             res.which_response = socket_message_CorrelationResponse_imu_calibrate_data_tag;
             res.response.imu_calibrate_data.success = peripherals.calibrateIMU();
         }},
    };

    socket.on<socket_message_CorrelationRequest>([&](const socket_message_CorrelationRequest &data, int clientId) {
        socket_message_CorrelationResponse res = socket_message_CorrelationResponse_init_default;
        res.correlation_id = data.correlation_id;
        res.status_code = 200;

        auto it = correlationHandlers.find(data.which_request);
        if (it != correlationHandlers.end()) {
            it->second(data, res);
            socket.emit(res, clientId);
        } else {
            printf("WARNING: no handler for correlation request: %d\n", data.which_request);
        }
    });
}

void IRAM_ATTR SpotControlLoopEntry(void *) {
    ESP_LOGI("main", "Control task starting");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = 5 / portTICK_PERIOD_MS;

    peripherals.begin();
    servoController.begin();
    motionService.begin();
    peripherals.calibrateIMU();

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
    ESP_LOGI("main", "Service task starting");

    wifiService.begin();
    MDNS.begin(APP_NAME);
    MDNS.setInstanceName(APP_NAME);
    apService.begin();

#if FT_ENABLED(USE_CAMERA)
    cameraService.begin();
#endif

    setupServer();

    socket.begin();
    setupEventSocket();

    ESP_LOGI("main", "Service task started");
    for (;;) {
        wifiService.loop();
        apService.loop();

        EXECUTE_EVERY_N_MS(2000, {
            socket_message_AnalyticsData analytics = socket_message_AnalyticsData_init_zero;
            system_service::getAnalytics(analytics);
            socket.emit(analytics);
        });

        EXECUTE_EVERY_N_MS(500, {
            socket_message_IMUData imu = socket_message_IMUData_init_zero;
            peripherals.getIMUProto(imu);
            socket.emit(imu);

            socket_message_RSSIData rssi = {.rssi = WiFi.RSSI()};
            socket.emit(rssi);
        });

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