#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <wifi/wifi_idf.h>
#include <functional>
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
#include <telemetry/telemetry.h>
#include <settings/placeholders.h>
#include <settings/imu_settings.h>
#include <algorithm>

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
Telemetry telemetry;

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

using ReplyFiller = std::function<void(socket_message_CorrelationResponse &)>;

// Answers a request later from another task. A client that left before the answer must not get it, nor whoever
// holds its socket now; `fill` sets the response, and may change the status from 200.
static std::function<void(const ReplyFiller &)> replyLater(const socket_message_CorrelationRequest &req, int clientId) {
    return
        [correlationId = req.correlation_id, clientId, session = wsSocket.session(clientId)](const ReplyFiller &fill) {
            auto reply = new socket_message_CorrelationResponse();
            *reply = socket_message_CorrelationResponse_init_default;
            reply->correlation_id = correlationId;
            reply->status_code = 200;
            fill(*reply);
            wsSocket.emitToSession(*reply, clientId, session);
            delete reply;
        };
}

// Runs bus work on the sensor task and replies from there, so the socket is not held up for its duration.
static void replyFromSensorTask(const socket_message_CorrelationRequest &req, socket_message_CorrelationResponse &res,
                                int clientId, ReplyFiller work) {
    auto reply = replyLater(req, clientId);
    if (peripherals.runOnSensorTask([reply, work = std::move(work)] { reply(work); })) {
        res.status_code = 0;
    } else {
        res.status_code = 503;
        strncpy(res.error_message, "Sensor task busy", sizeof(res.error_message) - 1);
    }
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
    FileSystemWS::fsHandler.setScheduling(
        [](std::function<void()> work) { return server.queueWork(std::move(work)); },
        [](int clientId, uint32_t ms) { return WebServer::waitWritable(clientId, ms); });
    wsSocket.onClose([](int clientId) { FileSystemWS::fsHandler.dropClient(clientId); });
    wsSocket.onSubscribed([](int32_t tag, int clientId) {
        if (tag != socket_message_Message_telemetry_batch_tag) return;
        telemetry.setRecording(true);
        static socket_message_TelemetryHeader header;
        header = socket_message_TelemetryHeader_init_zero;
        header.firmware_version = const_cast<char *>(APP_VERSION);
        header.build_target = const_cast<char *>(BUILD_TARGET);
        header.variant = const_cast<char *>(KINEMATICS_VARIANT_STR);
        header.device_id = const_cast<char *>(deviceId().c_str());
        header.imu_driver = const_cast<char *>(peripherals.imuDriverName());
        header.imu_rate_hz = peripherals.imuRateHz();
        header.mag_rate_hz = peripherals.magRateHz();
        header.control_rate_hz = 100;
        header.has_servo_settings = true;
        header.servo_settings = servoController.snapshot();
        header.has_imu_settings = true;
        header.imu_settings = effectiveImuSettings(peripherals.snapshot());
        header.batch_ticks = Telemetry::BATCH_TICKS;
        std::copy(std::begin(MotionService::JOINT_DIRECTION), std::end(MotionService::JOINT_DIRECTION),
                  header.joint_direction);
        wsSocket.emit(header, clientId);
    });

    wsSocket.on<socket_message_ControllerData>([&](const socket_message_ControllerData &data, int clientId) {
        const int64_t now = esp_timer_get_time();
        motionService.inbox.postInput(data, now / 1000, now);
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
             feature_service::features_request(robotService.name().c_str(), wifiService.getHostname().c_str(),
                                               res.response.features_data_response);
         }},

        {socket_message_CorrelationRequest_robot_name_update_tag,
         [](const auto &req, auto &res, int clientId) {
             if (!robotService.rename(req.request.robot_name_update.name)) res.status_code = 400;
             res.which_response = socket_message_CorrelationResponse_features_data_response_tag;
             feature_service::features_request(robotService.name().c_str(), wifiService.getHostname().c_str(),
                                               res.response.features_data_response);
         }},

        {socket_message_CorrelationRequest_i2c_scan_data_request_tag,
         [](const auto &req, auto &res, int clientId) {
             replyFromSensorTask(req, res, clientId, [](socket_message_CorrelationResponse &reply) {
                 reply.which_response = socket_message_CorrelationResponse_i2c_scan_data_tag;
                 peripherals.scanI2C();
                 peripherals.getI2CScanProto(reply.response.i2c_scan_data);
             });
         }},

        {socket_message_CorrelationRequest_imu_calibrate_execute_tag,
         [](const auto &req, auto &res, int clientId) {
             replyFromSensorTask(req, res, clientId, [](socket_message_CorrelationResponse &reply) {
                 reply.which_response = socket_message_CorrelationResponse_imu_calibrate_data_tag;
                 reply.response.imu_calibrate_data.success = peripherals.calibrateIMU();
             });
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
             mdnsService.queryAsync(req.request.mdns_query_request,
                                    [reply = replyLater(req, clientId)](const api_MDNSQueryResponse &result) {
                                        reply([&result](socket_message_CorrelationResponse &response) {
                                            response.which_response =
                                                socket_message_CorrelationResponse_mdns_query_response_tag;
                                            response.response.mdns_query_response = result;
                                        });
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

// Sensors wait on conversions, resets and echoes for up to seconds; they run below the control loop, which only
// copies out their latest readings. A 5 ms timer paces them, since the 100 Hz FreeRTOS tick cannot.
void sensorLoopEntry(void *) {
    static TaskHandle_t sensorTask = xTaskGetCurrentTaskHandle();
    peripherals.beginSensors();
    if (peripherals.imuRateHz() && !peripherals.calibrateIMU())
        ESP_LOGW("main", "Robot moved during gyro calibration; bias left at zero");
    const esp_timer_create_args_t pace = {
        .callback = [](void *) { xTaskNotifyGive(sensorTask); }, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK,
        .name = "sensor pace", .skip_unhandled_events = true};
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&pace, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 5000));
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        peripherals.sensorTick();
    }
}

// One control tick as the simulation needs it; angles leave the robot in radians.
static void recordTick(uint32_t seq, int64_t start, int64_t previousStart, int64_t computed, int64_t written,
                       const ImuSample &imu, ServoWrite write) {
    static socket_message_TickSample tick;  // about 340 bytes: kept off the control task's stack
    tick = socket_message_TickSample_init_zero;
    tick.seq = seq;
    tick.t_us = start;
    tick.period_us = previousStart ? start - previousStart : 0;
    tick.compute_us = computed - start;
    tick.has_imu = true;
    imuToProto(imu, tick.imu);
    const float *targets = motionService.getAngles();
    const float *angles = servoController.outputAngles();
    const uint16_t *pwm = servoController.outputPwm();
    for (int i = 0; i < 12; i++) {
        tick.angles[i] = DEG_TO_RAD_F(angles[i]);
        tick.targets[i] = DEG_TO_RAD_F(targets[i]);
        tick.pwm[i] = pwm[i];
    }
    tick.servo_write_us = write.attempted ? written - computed : 0;
    tick.servo_ok = write.ok;
    const CommandMsg &command = motionService.currentCommand();
    const float values[7] = {command.lx, command.ly, command.rx, command.ry, command.h, command.s, command.s1};
    std::copy(values, values + 7, tick.command);
    tick.command_rx_us = motionService.commandReceivedAt();
    tick.command_age_us = tick.command_rx_us ? start - static_cast<int64_t>(tick.command_rx_us) : 0;
    tick.mode = motionService.mode();
    tick.gait = motionService.gait();
    tick.link_lost = motionService.linkLost();
    telemetry.record(tick);
}

void IRAM_ATTR SpotControlLoopEntry(void *) {
    ESP_LOGI("main", "Control task starting");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10);

    peripherals.begin();
    xTaskCreatePinnedToCore(sensorLoopEntry, "Sensor task", 6144, nullptr, 4, nullptr, 1);
    servoController.begin();
    motionService.begin();
#if FT_ENABLED(USE_WS2812)
    ledService.begin();
#endif

    uint32_t tickSeq = 0;
    int64_t lastTickStart = 0;
    for (;;) {
        WARN_IF_SLOW(SpotControlLoopEntry, 10);
        const int64_t tickStart = esp_timer_get_time();
        const ImuSample imu = peripherals.imuSample();
        motionService.update(imu, peripherals.takeGesture());
        if (motionService.takeModeApplied()) {
            servoController.setMode(SERVO_CONTROL_STATE::ANGLE);
            motionService.isActive() ? servoController.activate() : servoController.deactivate();
        }
        servoController.setAngles(motionService.getAngles());
        const int64_t computed = esp_timer_get_time();
        const ServoWrite write = servoController.update();
        const int64_t written = esp_timer_get_time();
        if (telemetry.recording()) recordTick(tickSeq, tickStart, lastTickStart, computed, written, imu, write);
        tickSeq++;
        lastTickStart = tickStart;
#if FT_ENABLED(USE_WS2812)
        ledService.loop();
#endif
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// The robot owns its mode and gait: apps hear of a change on the service loop's next pass, and a newly
// opened app within a second, so after a reload it shows what the robot does rather than a default.
static void publishMotion() {
    static socket_message_ModesEnum lastMode = socket_message_ModesEnum_DEACTIVATED;
    static socket_message_WalkGaits lastGait = socket_message_WalkGaits_TROT;
    static uint32_t lastSentAt = 0;
    uint32_t now = esp_timer_get_time() / 1000;
    socket_message_ModesEnum mode = motionService.mode();
    socket_message_WalkGaits gait = motionService.gait();
    bool changed = mode != lastMode || gait != lastGait;
    if (!changed && now - lastSentAt < 1000) return;
    lastMode = mode;
    lastGait = gait;
    lastSentAt = now;
    wsSocket.emit(socket_message_ModeData {.mode = mode});
    wsSocket.emit(socket_message_WalkGaitData {.gait = gait});
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
    mdnsService.begin(wifiService.getHostname().c_str(), robotService.name().c_str());
    robotService.addUpdateHandler([](const std::string &) { mdnsService.setInstance(robotService.name().c_str()); }, false);
    wifiService.addUpdateHandler([](const std::string &) { mdnsService.setHostname(wifiService.getHostname().c_str()); },
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
            publishMotion();

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

        // Recording follows the subscribers, so a recorder that vanished without unsubscribing stops it too.
        const bool listening = wsSocket.hasSubscribers(socket_message_Message_telemetry_batch_tag);
        telemetry.setRecording(listening);
        if (!listening) telemetry.discardUnsent();
        static socket_message_TelemetryBatch batch;
        static uint32_t batchesSent = 0, batchesFailed = 0;
        while (telemetry.takeBatch(batch)) (wsSocket.emit(batch) ? batchesSent : batchesFailed)++;
        EXECUTE_EVERY_N_MS(1000, {
            if (telemetry.recording()) {
                socket_message_TelemetryNetwork network = socket_message_TelemetryNetwork_init_zero;
                network.t_us = esp_timer_get_time();
                network.rssi = WiFi.RSSI();
                network.channel = WiFi.channel();
                network.batches_sent = batchesSent;
                network.batches_failed = batchesFailed;
                network.ticks_dropped = telemetry.droppedTicks();
                wsSocket.emit(network);
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
