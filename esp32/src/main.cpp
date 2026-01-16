#include <Arduino.h>
#include <PsychicHttp.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Wire.h>
#include <map>

#include <filesystem.h>
#include <filesystem_ws.h>
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
    server.config.stack_size = 32768; // Increase from default 4KB to 32KB for large protobuf messages
    server.maxUploadSize = 1000000; // 1 MB;
    server.listen(80);
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
    // Set up filesystem handler callbacks for streaming transfers
    FileSystemWS::fsHandler.setSendCallbacks(
        // Send download metadata (file size, total chunks)
        [](const socket_message_FSDownloadMetadata& metadata, int clientId) {
            socket.emit(metadata, clientId);
        },
        // Send download data chunk
        [](const socket_message_FSDownloadData& data, int clientId) {
            socket.emit(data, clientId);
        },
        // Send download complete
        [](const socket_message_FSDownloadComplete& complete, int clientId) {
            socket.emit(complete, clientId);
        },
        // Send upload complete
        [](const socket_message_FSUploadComplete& complete, int clientId) {
            socket.emit(complete, clientId);
        }
    );

    socket.on<socket_message_ControllerData>(
        [&](const socket_message_ControllerData &data, int clientId) { motionService.handleInput(data); });

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

    // Handle streaming upload data (fire-and-forget from client)
    socket.on<socket_message_FSUploadData>([&](const socket_message_FSUploadData &data, int clientId) {
        FileSystemWS::fsHandler.handleUploadData(data);
    });

    using CorrelationHandler =
        std::function<void(const socket_message_CorrelationRequest &, socket_message_CorrelationResponse &, int)>;
    static std::map<pb_size_t, CorrelationHandler> correlationHandlers = {
        {socket_message_CorrelationRequest_features_data_request_tag, // Features data
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_features_data_response_tag;
             feature_service::features_request(req.request.features_data_request, res.response.features_data_response);
         }},

        {socket_message_CorrelationRequest_i2c_scan_data_request_tag, // i2c data
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_i2c_scan_data_tag;
             peripherals.scanI2C();
             peripherals.getI2CScanProto(res.response.i2c_scan_data);
         }},

        {socket_message_CorrelationRequest_imu_calibrate_execute_tag, // Calibration request
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_imu_calibrate_data_tag;
             res.response.imu_calibrate_data.success = peripherals.calibrateIMU();
         }},

         {socket_message_CorrelationRequest_system_information_request_tag, // All system information data
         [](const auto &req, auto &res, int clientId) {
            res.which_response = socket_message_CorrelationResponse_system_information_response_tag;
            res.response.system_information_response.has_analytics_data = true;
            res.response.system_information_response.has_static_system_information = true;
            system_service::getAnalytics(res.response.system_information_response.analytics_data);
            system_service::getStaticSystemInformation(res.response.system_information_response.static_system_information);
         }},

        // Filesystem operations
        {socket_message_CorrelationRequest_fs_delete_request_tag, // Delete file/directory
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_fs_delete_response_tag;
             res.response.fs_delete_response = FileSystemWS::fsHandler.handleDelete(req.request.fs_delete_request);
         }},

        {socket_message_CorrelationRequest_fs_mkdir_request_tag, // Create directory
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_fs_mkdir_response_tag;
             res.response.fs_mkdir_response = FileSystemWS::fsHandler.handleMkdir(req.request.fs_mkdir_request);
         }},

        {socket_message_CorrelationRequest_fs_list_request_tag, // List directory
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_fs_list_response_tag;
             res.response.fs_list_response = FileSystemWS::fsHandler.handleList(req.request.fs_list_request);
         }},

        {socket_message_CorrelationRequest_fs_download_request_tag, // Streaming download (no response, streams data)
         [](const auto &req, auto &res, int clientId) {
             // Download is handled differently - it streams chunks directly
             // No correlation response is sent; instead FSDownloadData/FSDownloadComplete are streamed
             FileSystemWS::fsHandler.handleDownloadRequest(req.request.fs_download_request, clientId);
             // Set status_code to 0 to indicate no response should be sent
             res.status_code = 0;
         }},

        {socket_message_CorrelationRequest_fs_upload_start_tag, // Upload start
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_fs_upload_start_response_tag;
             res.response.fs_upload_start_response = FileSystemWS::fsHandler.handleUploadStart(req.request.fs_upload_start, clientId);
         }},

        {socket_message_CorrelationRequest_fs_cancel_transfer_tag, // Cancel transfer
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_fs_cancel_transfer_response_tag;
             res.response.fs_cancel_transfer_response = FileSystemWS::fsHandler.handleCancelTransfer(req.request.fs_cancel_transfer);
         }},
    };

    socket.on<socket_message_CorrelationRequest>([&](const socket_message_CorrelationRequest &data, int clientId) {
        // Allocate response on heap to avoid stack overflow (CorrelationResponse is very large)
        auto res = new socket_message_CorrelationResponse();
        *res = socket_message_CorrelationResponse_init_default;
        res->correlation_id = data.correlation_id;
        res->status_code = 200;

        auto it = correlationHandlers.find(data.which_request);
        if (it != correlationHandlers.end()) {
            it->second(data, *res, clientId);
            // Only emit response if status_code is non-zero (streaming handlers set it to 0)
            if (res->status_code != 0) {
                socket.emit(*res, clientId);
            }
        } else {
            printf("WARNING: no handler for correlation request: %d\n", data.which_request);
        }

        delete res;
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

        EXECUTE_EVERY_N_MS(60000, {
            // Cleanup expired filesystem transfers
            FileSystemWS::fsHandler.cleanupExpiredTransfers();
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