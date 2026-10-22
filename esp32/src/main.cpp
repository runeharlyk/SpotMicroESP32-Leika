#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <nvs_flash.h>
#include <wifi/wifi_idf.h>
#include <functional>
#include <map>
#include <new>

#include <filesystem.h>
#include <filesystem_ws.h>
#include <peripherals/peripherals.h>
#include <peripherals/servo_controller.h>
#include <peripherals/led_service.h>
#include <peripherals/camera_service.h>
#include <communication/webserver.h>
#include <communication/websocket.h>
#include <communication/serial_link.h>
#include <communication/espnow_adapter.h>
#include <communication/handheld_controller.h>
#include <communication/nimble_link.h>
#include <features.h>
#include <motion.h>
#include <ota_flash.h>
#if USE_OTA_DOWNLOAD
#include <ota_http.h>
#endif
#include <animation/animation_store.h>
#include <core_dump_flash.h>
#include <wifi_service.h>
#include <ap_service.h>
#include <mdns_service.h>
#include <robot_service.h>
#include <system_service.h>
#include <telemetry/telemetry.h>
#include <telemetry/tick_sample.h>
#include <settings/placeholders.h>
#include <settings/imu_settings.h>
#include <variant.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#if CONFIG_IDF_TARGET_ESP32P4
#include <esp_hosted.h>
#endif

#include <www_mount.hpp>

Websocket wsSocket {server, "/api/ws"};
SerialLink serialLink;
#if CONFIG_BT_NIMBLE_ENABLED
// A link holds an encode buffer and a decoded message, 11 KB, and internal RAM is what Bluetooth runs short of.
static NimbleLink &bleLink =
    *new (heap_caps_malloc_prefer(sizeof(NimbleLink), 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_INTERNAL)) NimbleLink();
#endif

// Every link a client can be on: each takes the same messages, and a stream goes to its subscribers on each.
static CommAdapterBase *const links[] = {
    &wsSocket,
    &serialLink,
#if CONFIG_BT_NIMBLE_ENABLED
    &bleLink,
#endif
};

template <typename T>
static void onEveryLink(std::function<void(const T &, int)> handler) {
    for (CommAdapterBase *link : links) link->on<T>(handler);
}

/** Whether every subscriber on every link got the message. */
template <typename T>
static bool emitToAll(const T &data) {
    bool sent = true;
    for (CommAdapterBase *link : links) sent = link->emit(data) && sent;
    return sent;
}

static bool anySubscribers(int32_t tag) {
    return std::any_of(std::begin(links), std::end(links),
                       [tag](CommAdapterBase *link) { return link->hasSubscribers(tag); });
}

// The links that carry the telemetry stream: a batch a second of 100 ticks is more than Bluetooth carries.
static CommAdapterBase *const telemetryLinks[] = {&wsSocket, &serialLink};

template <typename T>
static bool emitTelemetry(const T &data) {
    bool sent = true;
    for (CommAdapterBase *link : telemetryLinks) sent = link->emit(data) && sent;
    return sent;
}

static bool anyTelemetrySubscribers() {
    return std::any_of(std::begin(telemetryLinks), std::end(telemetryLinks), [](CommAdapterBase *link) {
        return link->hasSubscribers(socket_message_Message_telemetry_batch_tag);
    });
}

Peripherals peripherals;
ServoController servoController;
MotionService motionService;

// The servo page's switch, applied by the control task between its writes (applyServoSwitch).
enum class ServoSwitch : uint8_t { NONE, ON, OFF };
static std::atomic<ServoSwitch> servoSwitch {ServoSwitch::NONE};

// A deactivated robot's servos wake limp, and move only once the page sets a PWM; an active one's go on driving the
// motion's angles.
static void applyServoSwitch() {
    const ServoSwitch request = servoSwitch.exchange(ServoSwitch::NONE);
    if (request == ServoSwitch::OFF) servoController.deactivate();
    if (request != ServoSwitch::ON) return;
    servoController.activate(motionService.isActive() ? SERVO_CONTROL_STATE::ANGLE : SERVO_CONTROL_STATE::PWM);
}

LEDService ledService;
#if USE_CAMERA
Camera::CameraService cameraService;
#endif
MDNSService mdnsService;

WiFiService wifiService;
APService apService;
RobotService robotService;
Telemetry telemetry;
EspOtaFlash otaFlash;
OtaSession otaSession {otaFlash};
#if USE_OTA_DOWNLOAD
EspHttpSource otaSource;
OtaDownload otaDownload {otaSession, otaSource};
#endif
anim::AnimationStore animationStore {FS_ANIMATION_DIRECTORY};
EspCoreDumpStore coreDumpStore;

// The variant the motion code and the servos run with: read at boot, switched by the control task.
static std::atomic<KinematicsVariant> activeVariant {socket_message_KinematicsVariant_KINEMATICS_VARIANT_UNSET};
static bool cameraDetected = false;

static bool cameraDisabled() {
    bool disabled = false;
    peripherals.read([&disabled](const PeripheralsConfiguration &settings) { disabled = settings.camera_disabled; });
    return disabled;
}

static void reportFeatures(socket_message_CorrelationResponse &res) {
    res.which_response = socket_message_CorrelationResponse_features_data_response_tag;
    const std::string name = robotService.name();
    const std::string hostname = wifiService.getHostname();
    feature_service::features_request({.robotName = name.c_str(),
                                       .hostname = hostname.c_str(),
                                       .variant = activeVariant.load(),
                                       .sensors = peripherals.status(),
                                       .servoDetected = servoController.detected(),
                                       .servosCalibrated = servoController.calibrated(),
                                       .cameraDetected = cameraDetected,
                                       .cameraActive = cameraDetected && !cameraDisabled(),
                                       .ws2812 = effectiveLedSettings(peripherals.snapshot()).enabled,
                                       .walkNN = motionService.walkNNAvailable()},
                                      res.response.features_data_response);
}

// The strip the settings ask for, dark when they name a pin it cannot be driven on. Saves arrive on the socket's task
// and IMU levelling on the sensor task.
static void driveLeds() {
    static std::atomic<int32_t> drivenPin {-1};
    const PeripheralsConfiguration settings = peripherals.snapshot();
    const api_LedSettings led = effectiveLedSettings(settings);
    const bool usable = usableLedSettings(led, settings, effectiveSonarSettings(settings), drivenPin);
    if (led.enabled && !usable) ESP_LOGW("main", "LED strip on GPIO %d cannot be driven; left dark", (int)led.pin);
    drivenPin = led.enabled && usable ? led.pin : -1;
    ledService.drive(drivenPin);
}

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

// An upload from the app, or the robot's own download from its claim on.
static bool updating() {
#if USE_OTA_DOWNLOAD
    if (otaDownload.busy()) return true;
#endif
    return otaSession.running();
}

// A robot taking on a mode during an update would move with its firmware half replaced; gestures are held back in the
// control task for the same reason.
static void requestMode(socket_message_ModesEnum mode) {
    if (!updating()) motionService.inbox.postMode(mode);
}

#if SOC_WIFI_SUPPORTED
// Posts what the handheld controller asks for as the socket's handlers post the app's messages, so the dead-man stop,
// the update's hold on the mode and the mode rules apply to it alike. Frames arrive on the WiFi task only.
static void handleControllerFrame(const uint8_t *data, int len) {
    static HandheldController controller;
    const auto packet = HandheldController::decode(data, len);
    if (!packet) return;
    const ControllerActions actions = controller.handle(*packet, motionService.mode(), motionService.gait());
    if (actions.input) {
        const int64_t now = esp_timer_get_time();
        motionService.inbox.postInput(*actions.input, now / 1000, now);
    }
    if (actions.mode) requestMode(*actions.mode);
    if (actions.gait) motionService.inbox.postGait(*actions.gait);
}

EspNowAdapter espNow {handleControllerFrame};

static bool espNowControllerEnabled() {
    bool enabled = false;
    peripherals.read([&enabled](const PeripheralsConfiguration &settings) { enabled = settings.espnow_controller; });
    return enabled;
}
#endif

static void replyOta(socket_message_CorrelationResponse &res, OtaReply reply) {
    res.status_code = reply.status;
    strncpy(res.error_message, reply.reason, sizeof(res.error_message) - 1);
}

static uint32_t nowMs() { return esp_timer_get_time() / 1000; }

#if USE_OTA_DOWNLOAD
static void otaStatusToProto(const OtaDownload::Status &status, socket_message_OtaStatus &proto) {
    proto.state = static_cast<socket_message_OtaDownloadState>(status.state);
    proto.written = status.written;
    proto.size = status.size;
    strncpy(proto.error, status.error, sizeof(proto.error) - 1);
}

// Every change of state, and every 500 ms while the robot downloads, so an app sees it progress.
static void publishOtaStatus() {
    static OtaDownload::Status last;
    static uint32_t lastSentAt = 0;
    if (!anySubscribers(socket_message_Message_ota_status_tag)) return;
    const OtaDownload::Status now = otaDownload.status();
    const uint32_t nowAt = nowMs();
    const bool changed = now.state != last.state;
    if (!changed && (now.state != OtaDownloadState::DOWNLOADING || nowAt - lastSentAt < 500)) return;
    last = now;
    lastSentAt = nowAt;
    socket_message_OtaStatus status = socket_message_OtaStatus_init_zero;
    otaStatusToProto(now, status);
    emitToAll(status);
}
#endif

static void goalStatusToProto(const MotionService::GoalStatus &status, socket_message_GoalStatus &proto) {
    proto.id = status.id;
    proto.state = static_cast<socket_message_GoalState>(status.state);
    proto.forward_m = status.goal.forward;
    proto.sideways_m = status.goal.sideways;
    proto.turn_deg = RAD_TO_DEG_F(status.goal.turn);
    proto.progress = status.goal.turn != 0 ? RAD_TO_DEG_F(status.progress) : status.progress;
    proto.timeout_s = status.timeout;
}

using ReplyFiller = std::function<void(socket_message_CorrelationResponse &)>;

/** Who sent a request: the link it came on, WebSocket or serial, and the client on that link. */
struct Client {
    CommAdapterBase *link;
    int id;
};

// Answers a request later from another task, on the link it came on. A client that left before the answer must not
// get it, nor whoever holds its socket now; `fill` sets the response, and may change the status from 200.
static std::function<void(const ReplyFiller &)> replyLater(const socket_message_CorrelationRequest &req, Client client) {
    return [correlationId = req.correlation_id, client, session = client.link->session(client.id)](
               const ReplyFiller &fill) {
        auto reply = new (std::nothrow) socket_message_CorrelationResponse();
        if (!reply) {
            ESP_LOGE("main", "No memory to answer request %u", (unsigned)correlationId);
            return;
        }
        *reply = socket_message_CorrelationResponse_init_default;
        reply->correlation_id = correlationId;
        reply->status_code = 200;
        fill(*reply);
        client.link->emitToSession(*reply, client.id, session);
        delete reply;
    };
}

// Sleeps the sensor task until its 5 ms pacing timer fires; called on the sensor task only.
static void waitForSensorPace() { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }

// Runs bus work on the sensor task and replies from there, so the socket is not held up for its duration.
static void replyFromSensorTask(const socket_message_CorrelationRequest &req, socket_message_CorrelationResponse &res,
                                Client client, ReplyFiller work) {
    auto reply = replyLater(req, client);
    if (peripherals.runOnSensorTask([reply, work = std::move(work)] { reply(work); })) {
        res.status_code = 0;
    } else {
        res.status_code = 503;
        strncpy(res.error_message, "Sensor task busy", sizeof(res.error_message) - 1);
    }
}

// Requests waiting for the control task to switch the variant; answered from the server's task once it has.
static std::mutex variantRepliesMutex;
static std::vector<std::function<void(const ReplyFiller &)>> variantReplies;

static void finishVariantSwitch() {
    mdnsService.setVariant(variantName(activeVariant.load()));
    std::vector<std::function<void(const ReplyFiller &)>> replies;
    {
        std::lock_guard<std::mutex> lock(variantRepliesMutex);
        replies.swap(variantReplies);
    }
    for (const auto &reply : replies) reply([](socket_message_CorrelationResponse &res) { reportFeatures(res); });
}

void setupServer() {
    server.config(50 + webAssetCount(), 16384);

#if USE_CAMERA
    if (cameraDetected)
        server.on("/api/camera/stream", HTTP_GET,
                  [&](httpd_req_t *request) { return cameraService.cameraStream(request); });
#endif
    wsSocket.begin();
    mountWebApp(server);
    server.addDefaultHeader("Server", APP_NAME);
}

// What a recording needs to read the batches that follow, sent to a client as it subscribes to them.
static void sendTelemetryHeader(CommAdapterBase &link, int clientId) {
    telemetry.setRecording(true);
    // Kept off the tasks' stacks, and both the WebSocket's and the serial link's tasks may be here.
    static std::mutex headerMutex;
    std::lock_guard<std::mutex> lock(headerMutex);
    static socket_message_TelemetryHeader header;
    header = socket_message_TelemetryHeader_init_zero;
    header.firmware_version = const_cast<char *>(APP_VERSION);
    header.build_target = const_cast<char *>(feature_service::buildTarget());
    header.variant = const_cast<char *>(variantName(activeVariant.load()));
    header.device_id = const_cast<char *>(deviceId().c_str());
    header.imu_driver = const_cast<char *>(peripherals.imuDriverName());
    header.imu_rate_hz = peripherals.imuRateHz();
    header.mag_rate_hz = peripherals.magRateHz();
    header.control_rate_hz = 100;
    header.has_servo_settings = true;
    // Through the settings reader, so the header carries the variant's joint model with the centres.
    servoController.protoHandler.read(header.servo_settings);
    header.has_imu_settings = true;
    header.imu_settings = effectiveImuSettings(peripherals.snapshot());
    header.batch_ticks = Telemetry::BATCH_TICKS;
    std::copy(std::begin(JOINT_DIRECTION), std::end(JOINT_DIRECTION), header.joint_direction);
    link.emit(header, clientId);
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
    wsSocket.onOpen([](int clientId) { confirmRunningFirmware(); });
    wsSocket.onClose([](int clientId) {
        FileSystemWS::fsHandler.dropClient(clientId);
        otaSession.drop(clientId);
        motionService.inbox.dropClient(clientId);
    });
    for (CommAdapterBase *link : telemetryLinks)
        link->onSubscribed([link](int32_t tag, int clientId) {
            if (tag == socket_message_Message_telemetry_batch_tag) sendTelemetryHeader(*link, clientId);
        });

    onEveryLink<socket_message_ControllerData>([&](const socket_message_ControllerData &data, int clientId) {
        const int64_t now = esp_timer_get_time();
        motionService.inbox.postInput(data, now / 1000, now);
    });

    onEveryLink<socket_message_ModeData>(
        [&](const socket_message_ModeData &data, int clientId) { requestMode(data.mode); });

    onEveryLink<socket_message_WalkGaitData>(
        [&](const socket_message_WalkGaitData &data, int clientId) { motionService.inbox.postGait(data.gait); });

    onEveryLink<socket_message_ServoPWMData>([&](const socket_message_ServoPWMData &data, int clientId) {
        servoController.setServoPWM(data.servo_id, data.servo_pwm);
    });

    onEveryLink<socket_message_ServoStateData>([&](const socket_message_ServoStateData &data, int clientId) {
        servoSwitch.store(data.active ? ServoSwitch::ON : ServoSwitch::OFF);
    });

    wsSocket.on<socket_message_FSUploadData>(
        [&](const socket_message_FSUploadData &data, int clientId) { FileSystemWS::fsHandler.handleUploadData(data); });

    using CorrelationHandler =
        std::function<void(const socket_message_CorrelationRequest &, socket_message_CorrelationResponse &, Client)>;
    static std::map<pb_size_t, CorrelationHandler> correlationHandlers = {
        {socket_message_CorrelationRequest_features_data_request_tag,
         [](const auto &req, auto &res, Client client) { reportFeatures(res); }},

        {socket_message_CorrelationRequest_robot_name_update_tag,
         [](const auto &req, auto &res, Client client) {
             if (!robotService.rename(req.request.robot_name_update.name)) res.status_code = 400;
             reportFeatures(res);
         }},

        // Only with the legs at rest: a new geometry would make them jump. The control task switches, and the reply
        // leaves once it has, so it reports the new variant.
        {socket_message_CorrelationRequest_robot_variant_update_tag,
         [](const auto &req, auto &res, Client client) {
             const KinematicsVariant variant = req.request.robot_variant_update.variant;
             auto refuse = [&res](uint32_t status, const char *reason) {
                 res.status_code = status;
                 strncpy(res.error_message, reason, sizeof(res.error_message) - 1);
                 reportFeatures(res);
             };
             if (!knownVariant(variant)) return refuse(400, "Unknown variant");
             if (motionService.mode() != socket_message_ModesEnum_DEACTIVATED) return refuse(409, "Deactivate first");
             if (!robotService.chooseVariant(variant)) return refuse(400, "Variant not stored");
             if (variant == activeVariant.load()) return reportFeatures(res);
             {
                 std::lock_guard<std::mutex> lock(variantRepliesMutex);
                 variantReplies.push_back(replyLater(req, client));
             }
             motionService.inbox.postVariant(variant);
             res.status_code = 0;
         }},

        {socket_message_CorrelationRequest_i2c_scan_data_request_tag,
         [](const auto &req, auto &res, Client client) {
             replyFromSensorTask(req, res, client, [](socket_message_CorrelationResponse &reply) {
                 reply.which_response = socket_message_CorrelationResponse_i2c_scan_data_tag;
                 peripherals.scanI2C();
                 peripherals.getI2CScanProto(reply.response.i2c_scan_data);
             });
         }},

        {socket_message_CorrelationRequest_imu_calibrate_execute_tag,
         [](const auto &req, auto &res, Client client) {
             replyFromSensorTask(req, res, client, [](socket_message_CorrelationResponse &reply) {
                 reply.which_response = socket_message_CorrelationResponse_imu_calibrate_data_tag;
                 const Peripherals::ImuCalibration result = peripherals.calibrateIMU(true, waitForSensorPace);
                 reply.response.imu_calibrate_data.success = result.still;
                 reply.response.imu_calibrate_data.levelled = result.levelled;
                 reply.response.imu_calibrate_data.tilt_deg = result.tiltDeg;
             });
         }},

        {socket_message_CorrelationRequest_system_information_request_tag,
         [](const auto &req, auto &res, Client client) {
             res.which_response = socket_message_CorrelationResponse_system_information_response_tag;
             res.response.system_information_response.has_analytics_data = true;
             res.response.system_information_response.has_static_system_information = true;
             system_service::getAnalytics(res.response.system_information_response.analytics_data);
             system_service::getStaticSystemInformation(
                 res.response.system_information_response.static_system_information);
         }},

        {socket_message_CorrelationRequest_fs_delete_request_tag,
         [](const auto &req, auto &res, Client client) {
             res.which_response = socket_message_CorrelationResponse_fs_delete_response_tag;
             res.response.fs_delete_response = FileSystemWS::fsHandler.handleDelete(req.request.fs_delete_request);
         }},

        {socket_message_CorrelationRequest_fs_mkdir_request_tag,
         [](const auto &req, auto &res, Client client) {
             res.which_response = socket_message_CorrelationResponse_fs_mkdir_response_tag;
             res.response.fs_mkdir_response = FileSystemWS::fsHandler.handleMkdir(req.request.fs_mkdir_request);
         }},

        {socket_message_CorrelationRequest_fs_list_request_tag,
         [](const auto &req, auto &res, Client client) {
             res.which_response = socket_message_CorrelationResponse_fs_list_response_tag;
             res.response.fs_list_response = FileSystemWS::fsHandler.handleList(req.request.fs_list_request);
         }},

        // Accepted before the file streams as download messages, so a long download is not a late reply.
        {socket_message_CorrelationRequest_fs_download_request_tag,
         [](const auto &req, auto &res, Client client) {
             res.status_code = 202;
             client.link->emit(res, client.id);
             FileSystemWS::fsHandler.handleDownloadRequest(req.request.fs_download_request, client.id);
             res.status_code = 0;
         }},

        {socket_message_CorrelationRequest_fs_upload_start_tag,
         [](const auto &req, auto &res, Client client) {
             res.which_response = socket_message_CorrelationResponse_fs_upload_start_response_tag;
             res.response.fs_upload_start_response =
                 FileSystemWS::fsHandler.handleUploadStart(req.request.fs_upload_start, client.id);
         }},

        {socket_message_CorrelationRequest_fs_cancel_transfer_tag,
         [](const auto &req, auto &res, Client client) {
             res.which_response = socket_message_CorrelationResponse_fs_cancel_transfer_response_tag;
             res.response.fs_cancel_transfer_response =
                 FileSystemWS::fsHandler.handleCancelTransfer(req.request.fs_cancel_transfer);
         }},

        {socket_message_CorrelationRequest_wifi_settings_request_tag,
         [](const auto &req, auto &res, Client client) {
             replyWithSettings(wifiService.protoHandler, res, socket_message_CorrelationResponse_wifi_settings_tag,
                               res.response.wifi_settings);
         }},

        {socket_message_CorrelationRequest_wifi_settings_tag,
         [](const auto &req, auto &res, Client client) {
             applySettings(wifiService.protoHandler, req.request.wifi_settings, res,
                           socket_message_CorrelationResponse_wifi_settings_tag, res.response.wifi_settings);
         }},

        {socket_message_CorrelationRequest_wifi_status_request_tag,
         [](const auto &req, auto &res, Client client) {
             res.which_response = socket_message_CorrelationResponse_wifi_status_tag;
             WiFiService::status(res.response.wifi_status);
         }},

        {socket_message_CorrelationRequest_wifi_scan_start_tag,
         [](const auto &req, auto &res, Client client) { WiFiService::startScan(); }},

        {socket_message_CorrelationRequest_wifi_networks_request_tag,
         [](const auto &req, auto &res, Client client) {
             if (WiFiService::scanResults(res.response.wifi_network_list))
                 res.which_response = socket_message_CorrelationResponse_wifi_network_list_tag;
             else
                 res.status_code = 202;
         }},

        {socket_message_CorrelationRequest_ap_settings_request_tag,
         [](const auto &req, auto &res, Client client) {
             replyWithSettings(apService.protoHandler, res, socket_message_CorrelationResponse_ap_settings_tag,
                               res.response.ap_settings);
         }},

        {socket_message_CorrelationRequest_ap_settings_tag,
         [](const auto &req, auto &res, Client client) {
             applySettings(apService.protoHandler, req.request.ap_settings, res,
                           socket_message_CorrelationResponse_ap_settings_tag, res.response.ap_settings);
         }},

        {socket_message_CorrelationRequest_ap_status_request_tag,
         [](const auto &req, auto &res, Client client) {
             res.which_response = socket_message_CorrelationResponse_ap_status_tag;
             apService.statusProto(res.response.ap_status);
         }},

        {socket_message_CorrelationRequest_mdns_status_request_tag,
         [](const auto &req, auto &res, Client client) {
             res.which_response = socket_message_CorrelationResponse_mdns_status_tag;
             mdnsService.status(res.response.mdns_status);
         }},

        // The query runs in its own task and replies from there, so the socket is not held up.
        {socket_message_CorrelationRequest_mdns_query_request_tag,
         [](const auto &req, auto &res, Client client) {
             mdnsService.queryAsync(req.request.mdns_query_request,
                                    [reply = replyLater(req, client)](const api_MDNSQueryResponse &result) {
                                        reply([&result](socket_message_CorrelationResponse &response) {
                                            response.which_response =
                                                socket_message_CorrelationResponse_mdns_query_response_tag;
                                            response.response.mdns_query_response = result;
                                        });
                                    });
             res.status_code = 0;
         }},

#if USE_CAMERA && USE_DVP_CAMERA
        {socket_message_CorrelationRequest_camera_settings_request_tag,
         [](const auto &req, auto &res, Client client) {
             replyWithSettings(cameraService.protoHandler, res, socket_message_CorrelationResponse_camera_settings_tag,
                               res.response.camera_settings);
         }},

        {socket_message_CorrelationRequest_camera_settings_tag,
         [](const auto &req, auto &res, Client client) {
             applySettings(cameraService.protoHandler, req.request.camera_settings, res,
                           socket_message_CorrelationResponse_camera_settings_tag, res.response.camera_settings);
         }},
#endif

        {socket_message_CorrelationRequest_servo_settings_request_tag,
         [](const auto &req, auto &res, Client client) {
             replyWithSettings(servoController.protoHandler, res,
                               socket_message_CorrelationResponse_servo_settings_tag, res.response.servo_settings);
         }},

        {socket_message_CorrelationRequest_servo_settings_tag,
         [](const auto &req, auto &res, Client client) {
             applySettings(servoController.protoHandler, req.request.servo_settings, res,
                           socket_message_CorrelationResponse_servo_settings_tag, res.response.servo_settings);
         }},

        {socket_message_CorrelationRequest_peripheral_settings_request_tag,
         [](const auto &req, auto &res, Client client) {
             replyWithSettings(peripherals.protoHandler, res,
                               socket_message_CorrelationResponse_peripheral_settings_tag,
                               res.response.peripheral_settings);
         }},

        {socket_message_CorrelationRequest_peripheral_settings_tag,
         [](const auto &req, auto &res, Client client) {
             applySettings(peripherals.protoHandler, req.request.peripheral_settings, res,
                           socket_message_CorrelationResponse_peripheral_settings_tag,
                           res.response.peripheral_settings);
         }},

        // Both defer the work by 250 ms, so this reply leaves before the device goes down.
        {socket_message_CorrelationRequest_odometry_request_tag,
         [](const auto &req, auto &res, Client client) {
             res.which_response = socket_message_CorrelationResponse_odometry_tag;
             odometryToProto(
                 req.request.odometry_request.reset ? motionService.resetOdometry() : motionService.odometry(),
                 res.response.odometry);
         }},

        // Checked here for the reply; the control task checks the mode again when it takes the goal. The reply
        // carries the goal's number, and its timeout follows in the status once the control task has started it.
        {socket_message_CorrelationRequest_goal_move_tag,
         [](const auto &req, auto &res, Client client) {
             const auto &move = req.request.goal_move;
             auto refuse = [&res](uint32_t status, const char *reason) {
                 res.status_code = status;
                 strncpy(res.error_message, reason, sizeof(res.error_message) - 1);
             };
             const Goal goal {move.forward_m, move.sideways_m, DEG_TO_RAD_F(move.turn_deg),
                              move.speed == 0 ? 0.5f : move.speed};
             if (!GoalController::valid(goal)) return refuse(400, "One target, at a speed up to 1");
             const socket_message_ModesEnum mode = motionService.mode();
             if (mode != socket_message_ModesEnum_STAND && mode != socket_message_ModesEnum_WALK)
                 return refuse(409, "Stand up first");
             if (updating()) return refuse(409, "Updating");
             MotionService::GoalStatus accepted;
             accepted.id = motionService.inbox.postGoal(goal, client.link == &wsSocket ? client.id : -1);
             accepted.state = GoalState::RUNNING;
             accepted.goal = goal;
             res.status_code = 202;
             res.which_response = socket_message_CorrelationResponse_goal_status_tag;
             goalStatusToProto(accepted, res.response.goal_status);
         }},

        {socket_message_CorrelationRequest_goal_cancel_tag,
         [](const auto &req, auto &res, Client client) { motionService.inbox.postGoalCancel(); }},

        {socket_message_CorrelationRequest_goal_status_request_tag,
         [](const auto &req, auto &res, Client client) {
             res.which_response = socket_message_CorrelationResponse_goal_status_tag;
             goalStatusToProto(motionService.goalStatus(), res.response.goal_status);
         }},

        {socket_message_CorrelationRequest_system_restart_tag,
         [](const auto &req, auto &res, Client client) { system_service::restart(); }},

        {socket_message_CorrelationRequest_system_reset_tag,
         [](const auto &req, auto &res, Client client) { system_service::reset(); }},

        {socket_message_CorrelationRequest_ota_start_tag,
         [](const auto &req, auto &res, Client client) {
#if USE_OTA_DOWNLOAD
             if (otaDownload.busy()) return replyOta(res, {409, "An update is already running"});
#endif
             const bool deactivated = motionService.mode() == socket_message_ModesEnum_DEACTIVATED;
             replyOta(res, otaSession.start(req.request.ota_start.size, deactivated, client.id, nowMs()));
         }},

        {socket_message_CorrelationRequest_ota_chunk_tag,
         [](const auto &req, auto &res, Client client) {
             const auto &chunk = req.request.ota_chunk;
             const uint8_t *bytes = chunk.data ? chunk.data->bytes : nullptr;
             const size_t size = chunk.data ? chunk.data->size : 0;
             replyOta(res, otaSession.chunk(chunk.index, bytes, size, client.id, nowMs()));
         }},

        {socket_message_CorrelationRequest_ota_finish_tag,
         [](const auto &req, auto &res, Client client) { replyOta(res, otaSession.finish(client.id)); }},

#if USE_OTA_DOWNLOAD
        // TLS and the HTTP client want a deep stack, 8 KB as in ESP-IDF's OTA examples; the task ends with the download.
        {socket_message_CorrelationRequest_ota_download_tag,
         [](const auto &req, auto &res, Client client) {
             const bool deactivated = motionService.mode() == socket_message_ModesEnum_DEACTIVATED;
             const OtaReply claimed = otaDownload.claim(req.request.ota_download.url, deactivated);
             replyOta(res, claimed);
             if (claimed.status != 202) return;
             const auto download = [](void *) {
                 otaDownload.run([] { return motionService.mode() == socket_message_ModesEnum_DEACTIVATED; }, nowMs);
                 vTaskDelete(nullptr);
             };
             if (xTaskCreate(download, "ota_download", 8192, nullptr, 4, nullptr) != pdPASS) {
                 otaDownload.abandon("Out of memory");
                 return replyOta(res, {500, "Could not start the download"});
             }
             res.which_response = socket_message_CorrelationResponse_ota_status_tag;
             otaStatusToProto(otaDownload.status(), res.response.ota_status);
         }},
#else
        {socket_message_CorrelationRequest_ota_download_tag,
         [](const auto &req, auto &res, Client client) {
             replyOta(res, {400, "This board has no room for the robot's own download"});
         }},
#endif

        {socket_message_CorrelationRequest_animation_list_request_tag,
         [](const auto &req, auto &res, Client client) {
             res.which_response = socket_message_CorrelationResponse_animation_list_tag;
             auto &list = res.response.animation_list;
             for (const auto &entry : animationStore.list()) {
                 if (list.animations_count >= sizeof(list.animations) / sizeof(list.animations[0])) break;
                 auto &out = list.animations[list.animations_count++];
                 strncpy(out.name, entry.name.c_str(), sizeof(out.name) - 1);
                 out.builtin = entry.builtin;
                 out.size = entry.size;
             }
         }},

        // The clamp sweep is for the robot's own legs at their default height; without a variant there are none.
        {socket_message_CorrelationRequest_animation_validate_tag,
         [](const auto &req, auto &res, Client client) {
             res.which_response = socket_message_CorrelationResponse_animation_report_tag;
             auto &report = res.response.animation_report;
             const char *error = nullptr;
             const auto clip = animationStore.load(req.request.animation_validate.name, error);
             report.ok = clip != nullptr;
             if (!clip) {
                 strncpy(report.error, error, sizeof(report.error) - 1);
                 res.status_code = 422;
                 return;
             }
             strncpy(report.description, clip->description, sizeof(report.description) - 1);
             for (int i = 0; i < clip->paramCount; i++) {
                 const anim::ParamSpec &spec = clip->params[i];
                 report.params[report.params_count++] = {static_cast<animation_ParamId>(spec.id), spec.min,
                                                         spec.defaultValue, spec.max};
             }
             report.duration = clip->duration();
             report.loop = clip->loop;
             report.hold_end = clip->holdEnd;
             const KinematicsVariant variant = activeVariant.load();
             if (const KinConfig *config = kinConfigFor(variant)) {
                 Kinematics kin(*config);
                 report.clamped_mask = anim::clampSweep(*clip, kin, *jointModelFor(variant),
                                                        config->default_feet_positions, config->default_body_height);
             }
         }},

        // Loaded here, so the control task only swaps in a clip that is ready.
        {socket_message_CorrelationRequest_animation_play_tag,
         [](const auto &req, auto &res, Client client) {
             const auto &play = req.request.animation_play;
             auto refuse = [&res](uint32_t status, const char *reason) {
                 res.status_code = status;
                 strncpy(res.error_message, reason, sizeof(res.error_message) - 1);
             };
             const char *error = nullptr;
             MotionInbox::Play request {animationStore.load(play.name, error), {}, 0};
             if (!request.clip) return refuse(strcmp(error, "no such clip") == 0 ? 404 : 422, error);
             if (motionService.mode() == socket_message_ModesEnum_DEACTIVATED) return refuse(409, "Stand up first");
             for (pb_size_t i = 0; i < play.params_count && i < anim::PARAM_MAX; i++)
                 request.params[request.paramCount++] = {static_cast<int>(play.params[i].id), play.params[i].value};
             motionService.inbox.postPlay(request, client.link == &wsSocket ? client.id : -1);
         }},

        {socket_message_CorrelationRequest_animation_stop_tag,
         [](const auto &req, auto &res, Client client) { motionService.inbox.postStopAnimation(); }},

        {socket_message_CorrelationRequest_animation_pose_tag,
         [](const auto &req, auto &res, Client client) {
             const auto &request = req.request.animation_pose;
             auto refuse = [&res](uint32_t status, const char *reason) {
                 res.status_code = status;
                 strncpy(res.error_message, reason, sizeof(res.error_message) - 1);
             };
             anim::Pose pose;
             if (const char *error = anim::poseFromProto(request.body, request.legs, request.legs_count, pose))
                 return refuse(422, error);
             const socket_message_ModesEnum mode = motionService.mode();
             if (mode != socket_message_ModesEnum_STAND && mode != socket_message_ModesEnum_ANIMATE)
                 return refuse(409, "Stand first");
             if (mode == socket_message_ModesEnum_ANIMATE &&
                 motionService.animationStatus().state != anim::State::IDLE)
                 return refuse(409, "A clip is playing");
             motionService.inbox.postPose(pose, client.link == &wsSocket ? client.id : -1);
         }},
        {socket_message_CorrelationRequest_core_dump_request_tag,
         [](const auto &req, auto &res, Client client) {
             res.which_response = socket_message_CorrelationResponse_core_dump_report_tag;
             reportCoreDump(coreDumpStore, res.response.core_dump_report);
         }},

        // Accepted, then streamed as a file download is.
        {socket_message_CorrelationRequest_core_dump_download_tag,
         [](const auto &req, auto &res, Client client) {
             res.status_code = 202;
             client.link->emit(res, client.id);
             res.status_code = 0;
             const uint32_t size = coreDumpStore.size();
             if (size == 0) return FileSystemWS::fsHandler.refuseDownload("No core dump stored", client.id);
             std::unique_ptr<FileSystemWS::DownloadSource> source(new (std::nothrow)
                                                                       CoreDumpSource(coreDumpStore, size));
             if (!source) return FileSystemWS::fsHandler.refuseDownload("Out of memory", client.id);
             FileSystemWS::fsHandler.startDownload(std::move(source), size, client.id);
         }},

        {socket_message_CorrelationRequest_core_dump_erase_tag,
         [](const auto &req, auto &res, Client client) {
             if (motionService.mode() != socket_message_ModesEnum_DEACTIVATED) {
                 res.status_code = 409;
                 strncpy(res.error_message, "Deactivate first", sizeof(res.error_message) - 1);
             } else if (!coreDumpStore.erase()) {
                 res.status_code = 500;
                 strncpy(res.error_message, "Erasing the core dump failed", sizeof(res.error_message) - 1);
             }
             res.which_response = socket_message_CorrelationResponse_core_dump_report_tag;
             reportCoreDump(coreDumpStore, res.response.core_dump_report);
         }},
    };

    // File transfer, the core dump download and firmware updates go over the WebSocket only: their 16 KiB chunks
    // outgrow a serial frame, and a firmware image would take minutes over Bluetooth.
    static const pb_size_t WEBSOCKET_ONLY[] = {
        socket_message_CorrelationRequest_fs_delete_request_tag,   socket_message_CorrelationRequest_fs_mkdir_request_tag,
        socket_message_CorrelationRequest_fs_list_request_tag,     socket_message_CorrelationRequest_fs_download_request_tag,
        socket_message_CorrelationRequest_fs_upload_start_tag,     socket_message_CorrelationRequest_fs_cancel_transfer_tag,
        socket_message_CorrelationRequest_ota_start_tag,           socket_message_CorrelationRequest_ota_chunk_tag,
        socket_message_CorrelationRequest_ota_finish_tag,          socket_message_CorrelationRequest_core_dump_download_tag,
    };

    auto answer = [](const socket_message_CorrelationRequest &data, Client client) {
        // Without exceptions a failed new aborts the robot; a request the heap cannot answer now times out instead.
        auto res = new (std::nothrow) socket_message_CorrelationResponse();
        if (!res) {
            ESP_LOGE("main", "No memory to answer request %u", (unsigned)data.correlation_id);
            return;
        }
        *res = socket_message_CorrelationResponse_init_default;
        res->correlation_id = data.correlation_id;
        res->status_code = 200;

        auto it = correlationHandlers.find(data.which_request);
        const bool websocketOnly = std::find(std::begin(WEBSOCKET_ONLY), std::end(WEBSOCKET_ONLY),
                                             data.which_request) != std::end(WEBSOCKET_ONLY);
        if (client.link != &wsSocket && websocketOnly) {
            res->status_code = 400;
            strncpy(res->error_message, "Only available over WiFi", sizeof(res->error_message) - 1);
            client.link->emit(*res, client.id);
        } else if (it != correlationHandlers.end()) {
            it->second(data, *res, client);
            if (res->status_code != 0) client.link->emit(*res, client.id);
        } else {
            // 501 is answered to nothing else, so a client tells a build without the request (older, or without the feature)
            // from a refusal.
            res->status_code = 501;
            strncpy(res->error_message, "Unknown request", sizeof(res->error_message) - 1);
            client.link->emit(*res, client.id);
        }

        delete res;
    };
    for (CommAdapterBase *link : links)
        link->on<socket_message_CorrelationRequest>(
            [answer, link](const socket_message_CorrelationRequest &data, int clientId) { answer(data, {link, clientId}); });
}

// Sensors wait on conversions, resets and echoes for up to seconds; they run below the control loop, which only
// copies out their latest readings. A 5 ms timer paces them, since the 100 Hz FreeRTOS tick cannot.
void sensorLoopEntry(void *) {
    static TaskHandle_t sensorTask = xTaskGetCurrentTaskHandle();
    const esp_timer_create_args_t pace = {
        .callback = [](void *) { xTaskNotifyGive(sensorTask); }, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK,
        .name = "sensor pace", .skip_unhandled_events = true};
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&pace, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 5000));
    peripherals.beginSensors();
    // Only the gyro at boot: the robot may be standing anywhere.
    if (peripherals.imuRateHz() && !peripherals.calibrateIMU(false, waitForSensorPace).still)
        ESP_LOGW("main", "Robot moved during gyro calibration; bias left at zero");
    // Watched from here, after the boot's probe and gyro calibration, which together come close to the watchdog's 5 s.
    // A pass runs at most one piece of queued work; the longest, a probe that waits out a BNO055 which does not come
    // back from its reset, takes about 3 s.
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    for (;;) {
        waitForSensorPace();
        esp_task_wdt_reset();
        peripherals.sensorTick();
    }
}

static void recordTick(const TickTimes &times, const ImuSample &imu, ServoWrite write) {
    static socket_message_TickSample tick;  // about 340 bytes: kept off the control task's stack
    fillTickSample(tick, times, imu, write, motionService, servoController);
    telemetry.record(tick);
}

void SpotControlLoopEntry(void *) {
    ESP_LOGI("main", "Control task starting");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10);

    xTaskCreatePinnedToCore(sensorLoopEntry, "Sensor task", 8192, nullptr, 4, nullptr, 1);
    servoController.begin();
    motionService.begin();
    const char *sitError = nullptr;
    motionService.setSitClip(animationStore.load("sit", sitError));
    if (sitError) ESP_LOGW("main", "No sit clip for the down gesture: %s", sitError);

    // A hung tick would leave the servos holding their last pose; the watchdog restarts the board instead, and the
    // restart puts the PCA9685 to sleep.
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));

    uint32_t tickSeq = 0;
    int64_t lastTickStart = 0;
    for (;;) {
        esp_task_wdt_reset();
        WARN_IF_SLOW(SpotControlLoopEntry, 10);
        const int64_t tickStart = esp_timer_get_time();
        const ImuSample imu = peripherals.imuSample();
        const gesture_t gesture = peripherals.takeGesture();
        motionService.useServoSpeedLimit(servoController.speedLimit());
        motionService.update(imu, updating() ? gesture_t::eGestureNone : gesture);
        if (motionService.takeModeApplied())
            motionService.isActive() ? servoController.activate() : servoController.deactivate();
        applyServoSwitch();
        if (const auto variant = motionService.takeVariantApplied()) {
            servoController.useJointModel(jointModelFor(*variant));
            activeVariant = *variant;
            server.queueWork(finishVariantSwitch);
        }
        servoController.setAngles(motionService.getAngles());
        const int64_t computed = esp_timer_get_time();
        const ServoWrite write = servoController.update((tickStart - lastTickStart) / 1e6f);
        const int64_t written = esp_timer_get_time();
        if (telemetry.recording()) recordTick({tickSeq, tickStart, lastTickStart, computed, written}, imu, write);
        tickSeq++;
        lastTickStart = tickStart;
        ledService.loop();
        // A tick that overran (a servo board that times out, a slow bus) starts the schedule afresh: catching
        // up would run every missed tick back to back once the stall ends.
        if (xTaskDelayUntil(&xLastWakeTime, xFrequency) == pdFALSE) xLastWakeTime = xTaskGetTickCount();
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
    emitToAll(socket_message_ModeData {.mode = mode});
    emitToAll(socket_message_WalkGaitData {.gait = gait});
}

// Every change of the clip or its state, and every 200 ms while a clip runs, so an app sees it progress.
static void publishAnimationStatus() {
    static MotionService::AnimationStatus last {"", anim::State::IDLE, 0, 0};
    static uint32_t lastSentAt = 0;
    if (!anySubscribers(socket_message_Message_animation_status_tag)) return;
    const MotionService::AnimationStatus now = motionService.animationStatus();
    const uint32_t nowMs = esp_timer_get_time() / 1000;
    const bool changed = now.state != last.state || strcmp(now.name, last.name) != 0;
    if (!changed && (now.state == anim::State::IDLE || nowMs - lastSentAt < 200)) return;
    last = now;
    lastSentAt = nowMs;
    socket_message_AnimationStatus status = socket_message_AnimationStatus_init_zero;
    strncpy(status.name, now.name, sizeof(status.name) - 1);
    status.state = static_cast<socket_message_AnimationState>(now.state);
    status.t = now.t;
    status.clamped_mask = now.clampedMask;
    emitToAll(status);
}

// On every new goal and every change of its state, so a client waiting on one hears how it ended, over whichever
// link it started the goal on.
static void publishGoalStatus() {
    static MotionService::GoalStatus last;
    if (!anySubscribers(socket_message_Message_goal_status_tag)) return;
    const MotionService::GoalStatus now = motionService.goalStatus();
    if (now.id == last.id && now.state == last.state) return;
    last = now;
    socket_message_GoalStatus status = socket_message_GoalStatus_init_zero;
    goalStatusToProto(now, status);
    emitToAll(status);
}

// Held deactivated while the rail stays under the cut-off: an activation from the app or a gesture is undone here.
static void cutOffOnLowVoltage() {
    if (!peripherals.powerCutoff() || motionService.mode() == socket_message_ModesEnum_DEACTIVATED) return;
    ESP_LOGW("main", "Servo rail under the cut-off voltage for %u ms: deactivating",
             (unsigned)PowerGuard::CUTOFF_HOLD_MS);
    motionService.inbox.postMode(socket_message_ModesEnum_DEACTIVATED);
}

void serviceLoopEntry(void *) {
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
    mdnsService.begin(wifiService.getHostname().c_str(), robotService.name().c_str(), variantName(activeVariant.load()));
    robotService.addUpdateHandler([](const std::string &) { mdnsService.setInstance(robotService.name().c_str()); }, false);
    wifiService.addUpdateHandler([](const std::string &) { mdnsService.setHostname(wifiService.getHostname().c_str()); },
                                 false);
    apService.begin();

#if USE_CAMERA
    if (cameraDisabled()) ESP_LOGI("main", "Camera disabled in the peripheral settings");
    else cameraDetected = cameraService.begin() == ESP_OK;
#endif

    setupServer();
    setupEventSocket();
    serialLink.listen();
#if CONFIG_BT_NIMBLE_ENABLED
    const std::string robotName = robotService.name();
    bleLink.start(robotName.empty() ? wifiService.getHostname() : robotName);
#endif
    server.listen(80);

    ESP_LOGI("main", "Service task started");

    for (;;) {
        wifiService.loop();
        apService.loop();
#if SOC_WIFI_SUPPORTED
        espNow.follow(espNowControllerEnabled());
#endif

        EXECUTE_EVERY_N_MS(1000, otaSession.expire(nowMs()));

        EXECUTE_EVERY_N_MS(2000, {
            system_service::sampleCpuUsage();
            if (anySubscribers(socket_message_Message_analytics_tag)) {
                socket_message_AnalyticsData analytics = socket_message_AnalyticsData_init_zero;
                system_service::getAnalytics(analytics);
                emitToAll(analytics);
            }
        });

        EXECUTE_EVERY_N_MS(100, {
            cutOffOnLowVoltage();
            publishMotion();
            publishAnimationStatus();
#if USE_OTA_DOWNLOAD
            publishOtaStatus();
#endif
            publishGoalStatus();

            // Also on the console UART: a robot set up over its USB-serial chip shows it joining the network.
            if (wifiService.takeStatusChanged()) {
                api_WifiStatus status = api_WifiStatus_init_zero;
                WiFiService::status(status);
                emitToAll(status);
            }

            if (anySubscribers(socket_message_Message_imu_tag)) {
                socket_message_IMUData imu = socket_message_IMUData_init_zero;
                peripherals.getIMUProto(imu);
                emitToAll(imu);
            }

            if (anySubscribers(socket_message_Message_sonar_tag)) {
                socket_message_SonarData sonar = socket_message_SonarData_init_zero;
                peripherals.getSonarProto(sonar);
                emitToAll(sonar);
            }

            if (peripherals.status().powerActive && anySubscribers(socket_message_Message_power_tag)) {
                socket_message_PowerData power = socket_message_PowerData_init_zero;
                peripherals.getPowerProto(power);
                emitToAll(power);
            }

            if (anySubscribers(socket_message_Message_rssi_tag)) {
                socket_message_RSSIData rssi = {.rssi = WiFi.RSSI()};
                emitToAll(rssi);
            }
        });

        // Recording follows the subscribers, so a recorder that vanished without unsubscribing stops it too.
        const bool listening = anyTelemetrySubscribers();
        telemetry.setRecording(listening);
        if (!listening) telemetry.discardUnsent();
        static socket_message_TelemetryBatch batch;
        static uint32_t batchesSent = 0, batchesFailed = 0;
        while (telemetry.takeBatch(batch)) (emitTelemetry(batch) ? batchesSent : batchesFailed)++;
        EXECUTE_EVERY_N_MS(1000, {
            if (telemetry.recording()) {
                socket_message_TelemetryNetwork network = socket_message_TelemetryNetwork_init_zero;
                network.t_us = esp_timer_get_time();
                network.rssi = WiFi.RSSI();
                network.channel = WiFi.channel();
                network.batches_sent = batchesSent;
                network.batches_failed = batchesFailed;
                network.ticks_dropped = telemetry.droppedTicks();
                const RailSample rail = peripherals.takeRailSample();
                network.rail_voltage = rail.voltage;
                network.rail_voltage_min = rail.voltageMin;
                network.rail_current = rail.current;
                emitTelemetry(network);
            }
        });

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

extern "C" void app_main(void) {
    system_service::countFailedAllocations();
    system_service::prepareRestart();
    serialLink.begin();
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    FileSystem::init();

    ESP_LOGI("main", "Booting robot");

    feature_service::printFeatureConfiguration();

    // Before the tasks start, so none of them sees the robot without its variant or its I2C bus.
    robotService.begin();
    const KinematicsVariant bootVariant = robotService.variant();
    activeVariant = bootVariant;
    if (knownVariant(bootVariant)) ESP_LOGI("main", "Variant %s", variantName(bootVariant));
    else ESP_LOGW("main", "No variant chosen: the servos stay asleep until one is chosen in the app");
    motionService.useConfig(kinConfigFor(bootVariant));
    servoController.useJointModel(jointModelFor(bootVariant));
    peripherals.begin();
    driveLeds();
    peripherals.addUpdateHandler([](const std::string &) { driveLeds(); }, false);

    xTaskCreate(serviceLoopEntry, "Service task", 8192, nullptr, 2, nullptr);

    xTaskCreatePinnedToCore(SpotControlLoopEntry, "Control task", 8192, nullptr, 5, nullptr, 1);

    ESP_LOGI("main", "Finished booting");
}
