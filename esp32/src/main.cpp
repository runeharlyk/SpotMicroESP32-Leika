#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <wifi/wifi_idf.h>
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
#include <robot_service.h>
#include <system_service.h>

#if CONFIG_IDF_TARGET_ESP32P4
#include <esp_hosted.h>
#endif

#include <www_mount.hpp>

Websocket wsSocket {server, "/api/ws"};

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
RobotService robotService;

// Replies with the settings a service holds.
template <class Handler, class Proto>
static void replyWithSettings(Handler &handler, socket_message_CorrelationResponse &res, pb_size_t tag, Proto &reply) {
    res.which_response = tag;
    handler.read(reply);
}

// Applies settings and replies with the settings now in force: the new ones, or on refusal the
// unchanged ones together with the reason.
template <class Handler, class Proto>
static void applySettings(Handler &handler, const Proto &settings, socket_message_CorrelationResponse &res,
                          pb_size_t tag, Proto &reply) {
    if (handler.update(settings) == StateUpdateResult::ERROR) {
        res.status_code = 400;
        strncpy(res.error_message, "Invalid state", sizeof(res.error_message) - 1);
    }
    replyWithSettings(handler, res, tag, reply);
}

void setupServer() {
    server.config(50 + webAssetCount(), 16384);

#if USE_CAMERA
    server.on("/api/camera/stream", HTTP_GET,
              [&](httpd_req_t *request) { return cameraService.cameraStream(request); });
#endif
    wsSocket.begin();
    mountWebApp(server);
    server.addDefaultHeader("Server", APP_NAME);
}

void setupEventSocket() {
    FileSystemWS::fsHandler.setSendCallbacks(
        [](const socket_message_FSDownloadMetadata &metadata, int clientId) { wsSocket.emit(metadata, clientId); },
        [](const socket_message_FSDownloadData &data, int clientId) { wsSocket.emit(data, clientId); },
        [](const socket_message_FSDownloadComplete &complete, int clientId) { wsSocket.emit(complete, clientId); },
        [](const socket_message_FSUploadComplete &complete, int clientId) { wsSocket.emit(complete, clientId); });

    wsSocket.on<socket_message_ControllerData>([&](const socket_message_ControllerData &data, int clientId) {
        motionService.inbox.postInput(data, esp_timer_get_time() / 1000);
    });

    wsSocket.on<socket_message_ModeData>(
        [&](const socket_message_ModeData &data, int clientId) { motionService.inbox.postMode(data.mode); });

    wsSocket.on<socket_message_WalkGaitData>(
        [&](const socket_message_WalkGaitData &data, int clientId) { motionService.inbox.postGait(data.gait); });

    wsSocket.on<socket_message_AnglesData>(
        [&](const socket_message_AnglesData &data, int clientId) { motionService.handleAngles(data); });

    wsSocket.on<socket_message_ServoPWMData>([&](const socket_message_ServoPWMData &data, int clientId) {
        servoController.setServoPWM(data.servo_id, data.servo_pwm);
    });

    wsSocket.on<socket_message_ServoStateData>([&](const socket_message_ServoStateData &data, int clientId) {
        data.active ? servoController.activate() : servoController.deactivate();
    });

    wsSocket.on<socket_message_FSUploadData>(
        [&](const socket_message_FSUploadData &data, int clientId) { FileSystemWS::fsHandler.handleUploadData(data); });

    using CorrelationHandler =
        std::function<void(const socket_message_CorrelationRequest &, socket_message_CorrelationResponse &, int)>;
    static std::map<pb_size_t, CorrelationHandler> correlationHandlers = {
        {socket_message_CorrelationRequest_features_data_request_tag,
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_features_data_response_tag;
             feature_service::features_request(robotService.name(), wifiService.getHostname(),
                                               res.response.features_data_response);
         }},

        {socket_message_CorrelationRequest_robot_name_update_tag,
         [](const auto &req, auto &res, int clientId) {
             if (!robotService.rename(req.request.robot_name_update.name)) res.status_code = 400;
             res.which_response = socket_message_CorrelationResponse_features_data_response_tag;
             feature_service::features_request(robotService.name(), wifiService.getHostname(),
                                               res.response.features_data_response);
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

        // Accepted before the file streams as download messages, so a long download is not a late reply.
        {socket_message_CorrelationRequest_fs_download_request_tag,
         [](const auto &req, auto &res, int clientId) {
             res.status_code = 202;
             wsSocket.emit(res, clientId);
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

        {socket_message_CorrelationRequest_wifi_settings_request_tag,
         [](const auto &req, auto &res, int clientId) {
             replyWithSettings(wifiService.protoHandler, res, socket_message_CorrelationResponse_wifi_settings_tag,
                               res.response.wifi_settings);
         }},

        {socket_message_CorrelationRequest_wifi_settings_tag,
         [](const auto &req, auto &res, int clientId) {
             applySettings(wifiService.protoHandler, req.request.wifi_settings, res,
                           socket_message_CorrelationResponse_wifi_settings_tag, res.response.wifi_settings);
         }},

        {socket_message_CorrelationRequest_wifi_status_request_tag,
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_wifi_status_tag;
             WiFiService::status(res.response.wifi_status);
         }},

        {socket_message_CorrelationRequest_wifi_scan_start_tag,
         [](const auto &req, auto &res, int clientId) { WiFiService::startScan(); }},

        {socket_message_CorrelationRequest_wifi_networks_request_tag,
         [](const auto &req, auto &res, int clientId) {
             if (WiFiService::scanResults(res.response.wifi_network_list))
                 res.which_response = socket_message_CorrelationResponse_wifi_network_list_tag;
             else
                 res.status_code = 202;
         }},

        {socket_message_CorrelationRequest_ap_settings_request_tag,
         [](const auto &req, auto &res, int clientId) {
             replyWithSettings(apService.protoHandler, res, socket_message_CorrelationResponse_ap_settings_tag,
                               res.response.ap_settings);
         }},

        {socket_message_CorrelationRequest_ap_settings_tag,
         [](const auto &req, auto &res, int clientId) {
             applySettings(apService.protoHandler, req.request.ap_settings, res,
                           socket_message_CorrelationResponse_ap_settings_tag, res.response.ap_settings);
         }},

        {socket_message_CorrelationRequest_ap_status_request_tag,
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_ap_status_tag;
             apService.statusProto(res.response.ap_status);
         }},

#if FT_ENABLED(USE_MDNS)
        {socket_message_CorrelationRequest_mdns_status_request_tag,
         [](const auto &req, auto &res, int clientId) {
             res.which_response = socket_message_CorrelationResponse_mdns_status_tag;
             mdnsService.status(res.response.mdns_status);
         }},

        // The query runs in its own task and replies from there, so the socket is not held up.
        {socket_message_CorrelationRequest_mdns_query_request_tag,
         [](const auto &req, auto &res, int clientId) {
             // A client that left before the answer must not get it, nor whoever holds its socket now.
             uint32_t session = wsSocket.session(clientId);
             mdnsService.queryAsync(req.request.mdns_query_request,
                                    [correlationId = req.correlation_id, clientId,
                                     session](const api_MDNSQueryResponse &result) {
                                        auto reply = new socket_message_CorrelationResponse();
                                        *reply = socket_message_CorrelationResponse_init_default;
                                        reply->correlation_id = correlationId;
                                        reply->status_code = 200;
                                        reply->which_response =
                                            socket_message_CorrelationResponse_mdns_query_response_tag;
                                        reply->response.mdns_query_response = result;
                                        wsSocket.emitToSession(*reply, clientId, session);
                                        delete reply;
                                    });
             res.status_code = 0;
         }},
#endif

#if USE_CAMERA && USE_DVP_CAMERA
        {socket_message_CorrelationRequest_camera_settings_request_tag,
         [](const auto &req, auto &res, int clientId) {
             replyWithSettings(cameraService.protoHandler, res, socket_message_CorrelationResponse_camera_settings_tag,
                               res.response.camera_settings);
         }},

        {socket_message_CorrelationRequest_camera_settings_tag,
         [](const auto &req, auto &res, int clientId) {
             applySettings(cameraService.protoHandler, req.request.camera_settings, res,
                           socket_message_CorrelationResponse_camera_settings_tag, res.response.camera_settings);
         }},
#endif

        {socket_message_CorrelationRequest_servo_settings_request_tag,
         [](const auto &req, auto &res, int clientId) {
             replyWithSettings(servoController.protoHandler, res,
                               socket_message_CorrelationResponse_servo_settings_tag, res.response.servo_settings);
         }},

        {socket_message_CorrelationRequest_servo_settings_tag,
         [](const auto &req, auto &res, int clientId) {
             applySettings(servoController.protoHandler, req.request.servo_settings, res,
                           socket_message_CorrelationResponse_servo_settings_tag, res.response.servo_settings);
         }},

        {socket_message_CorrelationRequest_peripheral_settings_request_tag,
         [](const auto &req, auto &res, int clientId) {
             replyWithSettings(peripherals.protoHandler, res,
                               socket_message_CorrelationResponse_peripheral_settings_tag,
                               res.response.peripheral_settings);
         }},

        {socket_message_CorrelationRequest_peripheral_settings_tag,
         [](const auto &req, auto &res, int clientId) {
             applySettings(peripherals.protoHandler, req.request.peripheral_settings, res,
                           socket_message_CorrelationResponse_peripheral_settings_tag,
                           res.response.peripheral_settings);
         }},

        // Both defer the work by 250 ms, so this reply leaves before the device goes down.
        {socket_message_CorrelationRequest_system_restart_tag,
         [](const auto &req, auto &res, int clientId) { system_service::restart(); }},

        {socket_message_CorrelationRequest_system_reset_tag,
         [](const auto &req, auto &res, int clientId) { system_service::reset(); }},
    };

    wsSocket.on<socket_message_CorrelationRequest>([&](const socket_message_CorrelationRequest &data, int clientId) {
        auto res = new socket_message_CorrelationResponse();
        *res = socket_message_CorrelationResponse_init_default;
        res->correlation_id = data.correlation_id;
        res->status_code = 200;

        auto it = correlationHandlers.find(data.which_request);
        if (it != correlationHandlers.end()) {
            it->second(data, *res, clientId);
            if (res->status_code != 0) {
                wsSocket.emit(*res, clientId);
            }
        } else {
            res->status_code = 400;
            strncpy(res->error_message, "Unknown request", sizeof(res->error_message) - 1);
            wsSocket.emit(*res, clientId);
        }

        delete res;
    });
}

// Sensors wait on conversions, resets and echoes for up to seconds; they run below the control loop, which
// only copies out their latest readings.
void sensorLoopEntry(void *) {
    peripherals.beginSensors();
    peripherals.calibrateIMU();
    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        peripherals.update();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10));
    }
}

void IRAM_ATTR SpotControlLoopEntry(void *) {
    ESP_LOGI("main", "Control task starting");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10);

    peripherals.begin();
    xTaskCreatePinnedToCore(sensorLoopEntry, "Sensor task", 4096, nullptr, 4, nullptr, 1);
    servoController.begin();
    motionService.begin();
#if FT_ENABLED(USE_WS2812)
    ledService.begin();
#endif

    for (;;) {
        WARN_IF_SLOW(SpotControlLoopEntry, 10);
        peripherals.update();
        motionService.update(&peripherals);
        if (motionService.takeModeApplied()) {
            servoController.setMode(SERVO_CONTROL_STATE::ANGLE);
            motionService.isActive() ? servoController.activate() : servoController.deactivate();
        }
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
#if CONFIG_IDF_TARGET_ESP32P4
    ESP_LOGI("main", "Initializing ESP-Hosted for C6 coprocessor WiFi...");
    int ret = esp_hosted_init();
    if (ret != 0) {
        ESP_LOGE("main", "ESP-Hosted init failed: %d", ret);
    } else {
        ESP_LOGI("main", "ESP-Hosted initialized, connecting to C6...");
        ret = esp_hosted_connect_to_slave();
        if (ret != 0) {
            ESP_LOGW("main", "ESP-Hosted connect failed: %d - WiFi may not work", ret);
        } else {
            ESP_LOGI("main", "ESP-Hosted link established with C6");
        }
    }
#endif

    WiFi.init();
    wifiService.begin();
    robotService.begin();
#if FT_ENABLED(USE_MDNS)
    mdnsService.begin(wifiService.getHostname(), robotService.name());
    robotService.addUpdateHandler([](const std::string &) { mdnsService.setInstance(robotService.name()); }, false);
    wifiService.addUpdateHandler([](const std::string &) { mdnsService.setHostname(wifiService.getHostname()); },
                                 false);
#endif
    apService.begin();

#if FT_ENABLED(USE_CAMERA)
    cameraService.begin();
#endif

    setupServer();
    setupEventSocket();
    server.listen(80);

    ESP_LOGI("main", "Service task started");

    for (;;) {
        wifiService.loop();
        apService.loop();

        EXECUTE_EVERY_N_MS(2000, {
            if (wsSocket.hasSubscribers(socket_message_Message_analytics_tag)) {
                socket_message_AnalyticsData analytics = socket_message_AnalyticsData_init_zero;
                system_service::getAnalytics(analytics);
                wsSocket.emit(analytics);
            }
        });

        EXECUTE_EVERY_N_MS(100, {
            if (wsSocket.hasSubscribers(socket_message_Message_imu_tag)) {
                socket_message_IMUData imu = socket_message_IMUData_init_zero;
                peripherals.getIMUProto(imu);
                wsSocket.emit(imu);
            }

            if (wsSocket.hasSubscribers(socket_message_Message_rssi_tag)) {
                socket_message_RSSIData rssi = {.rssi = WiFi.RSSI()};
                wsSocket.emit(rssi);
            }
        });

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    FileSystem::init();

    ESP_LOGI("main", "Booting robot");

    feature_service::printFeatureConfiguration();

    xTaskCreate(serviceLoopEntry, "Service task", 8192, nullptr, 2, nullptr);

    xTaskCreatePinnedToCore(SpotControlLoopEntry, "Control task", 8192, nullptr, 5, nullptr, 1);

    ESP_LOGI("main", "Finished booting");
}
