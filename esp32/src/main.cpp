#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <wifi/wifi_idf.h>
#include <mdns.h>
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
#include <eventbus.hpp>
#include <event_storage_manager.hpp>
#include <event_types.h>
#include <settings/camera_settings.h>

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
EventStorageManager storageManager;

static SubscriptionHandle modeHandle;

void setupServer() {
    server.config(50 + WWW_ASSETS_COUNT, 16384);
    server.listen(80);

    PROTO_ROUTE(server, "/api/servo/config", servo_settings, ServoSettings);
    PROTO_ROUTE(server, "/api/wifi/sta/settings", wifi_settings, WiFiSettings);
    PROTO_ROUTE(server, "/api/ap/settings", ap_settings, APSettings);
    PROTO_ROUTE(server, "/api/peripherals/settings", peripheral_settings, PeripheralsConfiguration);
#if FT_ENABLED(USE_MDNS)
    PROTO_ROUTE(server, "/api/mdns/settings", mdns_settings, MDNSSettings);
#endif
#if FT_ENABLED(USE_CAMERA) && USE_DVP_CAMERA
    PROTO_ROUTE(server, "/api/camera/settings", camera_settings, Camera::CameraSettings);
#endif

    system_service::registerRoutes(server);
    wifiService.registerRoutes(server);
    apService.registerRoutes(server);
#if FT_ENABLED(USE_MDNS)
    mdnsService.registerRoutes(server);
#endif
#if FT_ENABLED(USE_CAMERA)
    cameraService.registerRoutes(server);
#endif
    FileSystem::registerRoutes(server);

    wsSocket.begin();
#if EMBED_WEBAPP
    mountStaticAssets(server);
    mountSpaFallback(server);
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
        [](const socket_message_FSDownloadMetadata &metadata, int clientId) { wsSocket.emit(metadata, clientId); },
        [](const socket_message_FSDownloadData &data, int clientId) { wsSocket.emit(data, clientId); },
        [](const socket_message_FSDownloadComplete &complete, int clientId) { wsSocket.emit(complete, clientId); },
        [](const socket_message_FSUploadComplete &complete, int clientId) { wsSocket.emit(complete, clientId); });

    wsSocket.forward<socket_message_ControllerData>();
    wsSocket.forward<socket_message_ModeData>();
    wsSocket.forward<socket_message_WalkGaitData>();
    wsSocket.forward<socket_message_AnglesData>();
    wsSocket.forward<socket_message_ServoPWMData>();
    wsSocket.forward<socket_message_ServoStateData>();
    wsSocket.forward<socket_message_FSUploadData>();

    modeHandle = EventBus::instance().subscribe<socket_message_ModeData>([&](const socket_message_ModeData &data) {
        servoController.setMode(SERVO_CONTROL_STATE::ANGLE);
        motionService.handleMode(data);
        motionService.isActive() ? servoController.activate() : servoController.deactivate();
    });

    FileSystemWS::fsHandler.begin();

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
            printf("WARNING: no handler for correlation request: %d\n", data.which_request);
        }

        delete res;
    });
}

void IRAM_ATTR SpotControlLoopEntry(void *) {
    ESP_LOGI("main", "Control task starting");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10);

    peripherals.begin();
    servoController.begin();
    motionService.begin();
#if FT_ENABLED(USE_WS2812)
    ledService.begin();
#endif
    peripherals.calibrateIMU();

    for (;;) {
        WARN_IF_SLOW(SpotControlLoopEntry, 10);
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
    mdns_init();
    mdns_hostname_set(APP_NAME);
    mdns_instance_name_set(APP_NAME);
    apService.begin();

#if FT_ENABLED(USE_CAMERA)
    cameraService.begin();
#endif

    setupServer();
    setupEventSocket();

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

        EXECUTE_EVERY_N_MS(60000, { FileSystemWS::fsHandler.cleanupExpiredTransfers(); });

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
    storageManager.initialize();

    ESP_LOGI("main", "Booting robot");

    feature_service::printFeatureConfiguration();

    xTaskCreate(serviceLoopEntry, "Service task", 8192, nullptr, 2, nullptr);

    xTaskCreatePinnedToCore(SpotControlLoopEntry, "Control task", 8192, nullptr, 5, nullptr, 1);

    EventBus::instance().publish(SystemReadyEvent {});

    ESP_LOGI("main", "Finished booting");
}
