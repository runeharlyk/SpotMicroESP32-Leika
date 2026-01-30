#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <map>

#include <filesystem.h>
#include <filesystem_ws.h>
#include <peripherals/peripherals.h>
#include <peripherals/servo_controller.h>
#include <peripherals/led_service.h>
#include <peripherals/camera_service.h>
#include <communication/webserver.h>
#include <communication/websocket.h>
#include <features.h>
#include <motion.h>
#include <wifi_service.h>
#include <ap_service.h>
#include <mdns_service.h>
#include <system_service.h>

#include <www_mount.hpp>

Websocket socket {server, "/api/ws"};

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

WiFiService wifiService;
APService apService;

void setupServer() {
    server.config(50 + WWW_ASSETS_COUNT, 32768);
    server.listen(80);

    server.onProto("/api/system/reset", HTTP_POST,
              [&](httpd_req_t *request, api_Request *protoReq) { return system_service::handleReset(request); });
    server.onProto("/api/system/restart", HTTP_POST,
              [&](httpd_req_t *request, api_Request *protoReq) { return system_service::handleRestart(request); });
    server.onProto("/api/system/sleep", HTTP_POST,
              [&](httpd_req_t *request, api_Request *protoReq) { return system_service::handleSleep(request); });
#if USE_CAMERA
    server.on("/api/camera/still", HTTP_GET, [&](httpd_req_t *request) { return cameraService.cameraStill(request); });
    server.on("/api/camera/stream", HTTP_GET,
              [&](httpd_req_t *request) { return cameraService.cameraStream(request); });
    server.on("/api/camera/settings", HTTP_GET,
              [&](httpd_req_t *request) { return cameraService.protoEndpoint.getState(request); });
    server.onProto("/api/camera/settings", HTTP_POST, [&](httpd_req_t *request, api_Request *protoReq) {
        return cameraService.protoEndpoint.handleStateUpdate(request, protoReq);
    });
#endif
    server.on("/api/servo/config", HTTP_GET,
              [&](httpd_req_t *request) { return servoController.protoEndpoint.getState(request); });
    server.onProto("/api/servo/config", HTTP_POST, [&](httpd_req_t *request, api_Request *protoReq) {
        return servoController.protoEndpoint.handleStateUpdate(request, protoReq);
    });

    server.on("/api/wifi/sta/settings", HTTP_GET,
              [&](httpd_req_t *request) { return wifiService.protoEndpoint.getState(request); });
    server.onProto("/api/wifi/sta/settings", HTTP_POST, [&](httpd_req_t *request, api_Request *protoReq) {
        return wifiService.protoEndpoint.handleStateUpdate(request, protoReq);
    });
    server.on("/api/wifi/scan", HTTP_GET, [&](httpd_req_t *request) { return wifiService.handleScan(request); });
    server.on("/api/wifi/networks", HTTP_GET, [&](httpd_req_t *request) { return wifiService.getNetworks(request); });
    server.on("/api/wifi/sta/status", HTTP_GET,
              [&](httpd_req_t *request) { return wifiService.getNetworkStatus(request); });
	
    server.on("/api/ap/status", HTTP_GET, [&](httpd_req_t *request) { return apService.getStatusProto(request); });
    server.on("/api/ap/settings", HTTP_GET,
                    [&](httpd_req_t *request) { return apService.protoEndpoint.getState(request); });
    server.onProto("/api/ap/settings", HTTP_POST,
                         [&](httpd_req_t *request, api_Request *protoReq) {
                             return apService.protoEndpoint.handleStateUpdate(request, protoReq);
                         });
    
    // TODO: REMAKE TO PROTO - note: these are unused?
    server.on("/api/peripherals", HTTP_GET,
              [&](httpd_req_t *request) { return peripherals.endpoint.getState(request); });
    server.on("/api/peripherals", HTTP_POST, [&](httpd_req_t *request, JsonVariant &json) {
        return peripherals.endpoint.handleStateUpdate(request, json);
    });

#if FT_ENABLED(USE_MDNS)
    server.on("/api/mdns/settings", HTTP_GET, [&](httpd_req_t *request) { return mdnsService.protoEndpoint.getState(request); });
    server.onProto("/api/mdns/settings", HTTP_POST,
                   [&](httpd_req_t *request, api_Request *protoReq) {
                       return mdnsService.protoEndpoint.handleStateUpdate(request, protoReq);
                   });
    server.on("/api/mdns/status", HTTP_GET, [&](httpd_req_t *request) { return mdnsService.getStatus(request); });
    server.onProto("/api/mdns/query", HTTP_POST,
                   [&](httpd_req_t *request, api_Request *protoReq) {
                       return mdnsService.queryServices(request, protoReq);
                   });
#endif
    
    server.on("/api/config/*", HTTP_GET, [](httpd_req_t *request) { return FileSystem::getConfigFile(request); });
    server.on("/api/files", HTTP_GET, [&](httpd_req_t *request) { return FileSystem::getFilesProto(request); });
    PROTO_ENDPOINT(server, "/api/files/delete", file_delete_request, FileSystem::handleDelete);
    PROTO_ENDPOINT(server, "/api/files/edit", file_edit_request, FileSystem::handleEdit);
    PROTO_ENDPOINT(server, "/api/files/mkdir", file_mkdir_request, FileSystem::mkdir);
#if EMBED_WEBAPP
    mountStaticAssets(server);
#endif
    server.on("/*", HTTP_OPTIONS, [](httpd_req_t *request) {
        httpd_resp_set_status(request, "200 OK");
        return httpd_resp_send(request, nullptr, 0);
    });
    server.addDefaultHeader("Server", APP_NAME);
    server.addDefaultHeader("Access-Control-Allow-Origin", "*");
    server.addDefaultHeader("Access-Control-Allow-Headers", "Accept, Content-Type, Authorization");
    server.addDefaultHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.addDefaultHeader("Access-Control-Max-Age", "86400");
}

void setupEventSocket() {
    FileSystemWS::fsHandler.setSendCallbacks(
        [](const socket_message_FSDownloadMetadata &metadata, int clientId) { socket.emit(metadata, clientId); },
        [](const socket_message_FSDownloadData &data, int clientId) { socket.emit(data, clientId); },
        [](const socket_message_FSDownloadComplete &complete, int clientId) { socket.emit(complete, clientId); },
        [](const socket_message_FSUploadComplete &complete, int clientId) { socket.emit(complete, clientId); });

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

    socket.on<socket_message_FSUploadData>(
        [&](const socket_message_FSUploadData &data, int clientId) { FileSystemWS::fsHandler.handleUploadData(data); });

    using CorrelationHandler =
        std::function<void(const socket_message_CorrelationRequest &, socket_message_CorrelationResponse &, int)>;
    static std::map<pb_size_t, CorrelationHandler> correlationHandlers = {
        {socket_message_CorrelationRequest_features_data_request_tag,
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_features_data_response_tag;
             feature_service::features_request(req.request.features_data_request, res.response.features_data_response);
         }},

        {socket_message_CorrelationRequest_i2c_scan_data_request_tag,
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_i2c_scan_data_tag;
             peripherals.scanI2C();
             peripherals.getI2CScanProto(res.response.i2c_scan_data);
         }},

        {socket_message_CorrelationRequest_imu_calibrate_execute_tag,
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_imu_calibrate_data_tag;
             res.response.imu_calibrate_data.success = peripherals.calibrateIMU();
         }},

        {socket_message_CorrelationRequest_system_information_request_tag,
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_system_information_response_tag;
             res.response.system_information_response.has_analytics_data = true;
             res.response.system_information_response.has_static_system_information = true;
             system_service::getAnalytics(res.response.system_information_response.analytics_data);
             system_service::getStaticSystemInformation(
                 res.response.system_information_response.static_system_information);
         }},

        {socket_message_CorrelationRequest_fs_delete_request_tag,
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_fs_delete_response_tag;
             res.response.fs_delete_response = FileSystemWS::fsHandler.handleDelete(req.request.fs_delete_request);
         }},

        {socket_message_CorrelationRequest_fs_mkdir_request_tag,
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_fs_mkdir_response_tag;
             res.response.fs_mkdir_response = FileSystemWS::fsHandler.handleMkdir(req.request.fs_mkdir_request);
         }},

        {socket_message_CorrelationRequest_fs_list_request_tag,
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_fs_list_response_tag;
             res.response.fs_list_response = FileSystemWS::fsHandler.handleList(req.request.fs_list_request);
         }},

        {socket_message_CorrelationRequest_fs_download_request_tag,
         [](const auto &req, auto &res, int clientId) {
             FileSystemWS::fsHandler.handleDownloadRequest(req.request.fs_download_request, clientId);
             res.status_code = 0;
         }},

        {socket_message_CorrelationRequest_fs_upload_start_tag,
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_fs_upload_start_response_tag;
             res.response.fs_upload_start_response =
                 FileSystemWS::fsHandler.handleUploadStart(req.request.fs_upload_start, clientId);
         }},

        {socket_message_CorrelationRequest_fs_cancel_transfer_tag,
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_fs_cancel_transfer_response_tag;
             res.response.fs_cancel_transfer_response =
                 FileSystemWS::fsHandler.handleCancelTransfer(req.request.fs_cancel_transfer);
         }},
    };

    socket.on<socket_message_CorrelationRequest>([&](const socket_message_CorrelationRequest &data, int clientId) {
        auto res = new socket_message_CorrelationResponse();
        *res = socket_message_CorrelationResponse_init_default;
        res->correlation_id = data.correlation_id;
        res->status_code = 200;

        auto it = correlationHandlers.find(data.which_request);
        if (it != correlationHandlers.end()) {
            it->second(data, *res, clientId);
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
        WARN_IF_SLOW(SpotControlLoopEntry, 5);
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
            if (socket.hasSubscribers(socket_message_Message_analytics_tag)) {
                socket_message_AnalyticsData analytics = socket_message_AnalyticsData_init_zero;
                system_service::getAnalytics(analytics);
                socket.emit(analytics);
            }
        });

        EXECUTE_EVERY_N_MS(100, {
            if (socket.hasSubscribers(socket_message_Message_imu_tag)) {
                socket_message_IMUData imu = socket_message_IMUData_init_zero;
                peripherals.getIMUProto(imu);
                socket.emit(imu);
            }

            if (socket.hasSubscribers(socket_message_Message_rssi_tag)) {
                socket_message_RSSIData rssi = {.rssi = WiFi.RSSI()};
                socket.emit(rssi);
            }
        });

        EXECUTE_EVERY_N_MS(60000, { FileSystemWS::fsHandler.cleanupExpiredTransfers(); });

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void setup() {
    ESP_FS.begin();

    ESP_LOGI("main", "Booting robot");

    feature_service::printFeatureConfiguration();

    xTaskCreate(serviceLoopEntry, "Service task", 4096, nullptr, 2, nullptr);

    xTaskCreatePinnedToCore(SpotControlLoopEntry, "Control task", 4096, nullptr, 5, nullptr, 1);

    ESP_LOGI("main", "Finished booting");
}

void loop() { vTaskDelete(nullptr); }
