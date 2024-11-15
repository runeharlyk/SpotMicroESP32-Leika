#include <spot.h>

static const char *TAG = "Spot";

Spot::Spot()
    :
#if FT_ENABLED(USE_MOTION)
      _motionService(&_servoController)
#endif
{
}

void Spot::initialize() {
    ESP_LOGI(TAG, "Running Firmware Version: %s", APP_VERSION);
    ESPFS.begin(true);
    g_taskManager.begin();
#if FT_ENABLED(USE_WS2812)
    _ledService.loop();
#endif
    _wifiService.begin();

    setupServer();

    startServices();

    setupMDNS();

    ESP_LOGV(TAG, "Starting misc loop task");
    g_taskManager.createTask(this->_loopImpl, "Spot misc", 4096, this, 2, NULL, APPLICATION_CORE);
}

void Spot::setupServer() {
    _server.config.max_uri_handlers = _numberEndpoints;
    _server.maxUploadSize = _maxFileUpload;
    _server.listen(_port);

    // WIFI
    _server.on("/api/wifi/scan", HTTP_GET, _wifiService.handleScan);
    _server.on("/api/wifi/networks", HTTP_GET,
               [this](PsychicRequest *request) { return _wifiService.getNetworks(request); });
    _server.on("/api/wifi/sta/status", HTTP_GET,
               [this](PsychicRequest *request) { return _wifiService.getNetworkStatus(request); });
    _server.on("/api/wifi/sta/settings", HTTP_GET,
               [this](PsychicRequest *request) { return _wifiService.endpoint.getState(request); });
    _server.on("/api/wifi/sta/settings", HTTP_POST, [this](PsychicRequest *request, JsonVariant &json) {
        return _wifiService.endpoint.handleStateUpdate(request, json);
    });

    // AP
    _server.on("/api/wifi/ap/status", HTTP_GET,
               [this](PsychicRequest *request) { return _apService.getStatus(request); });
    _server.on("/api/wifi/ap/settings", HTTP_GET,
               [this](PsychicRequest *request) { return _apService.endpoint.getState(request); });
    _server.on("/api/wifi/ap/settings", HTTP_POST, [this](PsychicRequest *request, JsonVariant &json) {
        return _apService.endpoint.handleStateUpdate(request, json);
    });

    // CAMERA
    _server.on("/api/camera/still", HTTP_GET,
               [this](PsychicRequest *request) { return _cameraService.cameraStill(request); });
    _server.on("/api/camera/stream", HTTP_GET,
               [this](PsychicRequest *request) { return _cameraService.cameraStream(request); });
    _server.on("/api/camera/settings", HTTP_GET,
               [this](PsychicRequest *request) { return _cameraService.endpoint.getState(request); });
    _server.on("/api/camera/settings", HTTP_POST, [this](PsychicRequest *request, JsonVariant &json) {
        return _cameraService.endpoint.handleStateUpdate(request, json);
    });

    // SYSTEM
    _server.on("/api/system/reset", HTTP_POST, system_service::handleReset);
    _server.on("/api/system/restart", HTTP_POST, system_service::handleRestart);
    _server.on("/api/system/sleep", HTTP_POST, system_service::handleSleep);
    _server.on("/api/system/status", HTTP_GET, system_service::getStatus);
    _server.on("/api/system/metrics", HTTP_GET, system_service::getMetrics);

    // FILESYSTEM
    _server.on("/api/files", HTTP_GET, FileSystem::getFiles);
    _server.on("/api/files/delete", HTTP_POST, FileSystem::handleDelete);
    _server.on("/api/files/upload/*", HTTP_POST, FileSystem::uploadHandler);
    _server.on("/api/files/edit", HTTP_POST, FileSystem::handleEdit);

    // SERVO
    _server.on("/api/servo/config", HTTP_GET,
               [this](PsychicRequest *request) { return _servoController.endpoint.getState(request); });
    _server.on("/api/servo/config", HTTP_POST, [this](PsychicRequest *request, JsonVariant &json) {
        return _servoController.endpoint.handleStateUpdate(request, json);
    });

    // PERIPHERALS
    _server.on("/api/peripheral/settings", HTTP_GET,
               [this](PsychicRequest *request) { return _peripherals.endpoint.getState(request); });
    _server.on("/api/peripheral/settings", HTTP_POST, [this](PsychicRequest *request, JsonVariant &json) {
        return _peripherals.endpoint.handleStateUpdate(request, json);
    });

    // MISC
    _server.on("/api/ws/events", socket.getHandler());
    _server.on("/api/features", feature_service::getFeatures);
#if FT_ENABLED(USE_UPLOAD_FIRMWARE)
    _server.on("/api/firmware", HTTP_POST, _uploadFirmwareService.getHandler());
#endif
#if FT_ENABLED(USE_DOWNLOAD_FIRMWARE)
    _server.on("/api/firmware/download", HTTP_POST, [this](PsychicRequest *r, JsonVariant &json) {
        return _downloadFirmwareService.handleDownloadUpdate(r, json);
    });
#endif

#ifdef EMBED_WWW
    ESP_LOGV(TAG, "Registering routes from PROGMEM static resources");
    WWWData::registerRoutes([&](const String &uri, const String &contentType, const uint8_t *content, size_t len) {
        PsychicHttpRequestCallback requestHandler = [contentType, content, len](PsychicRequest *request) {
            PsychicResponse response(request);
            response.setCode(200);
            response.setContentType(contentType.c_str());
            response.addHeader("Content-Encoding", "gzip");
            response.addHeader("Cache-Control", "public, immutable, max-age=31536000");
            response.setContent(content, len);
            return response.send();
        };
        PsychicWebHandler *handler = new PsychicWebHandler();
        handler->onRequest(requestHandler);
        _server.on(uri.c_str(), HTTP_GET, handler);

        // Set default end-point for all non matching requests
        // this is easier than using webServer.onNotFound()
        if (uri.equals("/index.html")) {
            _server.defaultEndpoint->setHandler(handler);
        }
    });
#else
    // Serve static resources from /www/
    ESP_LOGV(TAG, "Registering routes from FS /www/ static resources");
    _server.serveStatic("/_app/", ESPFS, "/www/_app/");
    _server.serveStatic("/favicon.png", ESPFS, "/www/favicon.png");
    //  Serving all other get requests with "/www/index.htm"
    _server.onNotFound([](PsychicRequest *request) {
        if (request->method() == HTTP_GET) {
            PsychicFileResponse response(request, ESPFS, "/www/index.html", "text/html");
            return response.send();
            // String url = "http://" + request->host() + "/index.html";
            // request->redirect(url.c_str());
        }
    });
#endif
#ifdef SERVE_CONFIG_FILES
    _server.serveStatic("/api/config/", ESPFS, "/config/");
#endif

#if defined(ENABLE_CORS)
    ESP_LOGV(TAG, "Enabling CORS headers");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", CORS_ORIGIN);
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Accept, Content-Type, Authorization");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Credentials", "true");
#endif
    DefaultHeaders::Instance().addHeader("Server", _appName);
}

void Spot::setupMDNS() {
    ESP_LOGV(TAG, "Starting MDNS");
    MDNS.begin(_wifiService.getHostname());
    MDNS.setInstanceName(_appName);
    MDNS.addService("http", "tcp", _port);
    MDNS.addService("ws", "tcp", _port);
    MDNS.addServiceTxt("http", "tcp", "Firmware Version", APP_VERSION);
}

void Spot::startServices() {
    _apService.begin();
#if FT_ENABLED(USE_UPLOAD_FIRMWARE)
    _uploadFirmwareService.begin();
#endif
    _peripherals.begin();
    _servoController.begin();
#if FT_ENABLED(USE_MOTION)
    _motionService.begin();
#endif
#if FT_ENABLED(USE_CAMERA)
    _cameraService.begin();
#endif
}

void IRAM_ATTR Spot::loop() {
    while (1) {
        _wifiService.loop();
        _apService.loop();
        EXECUTE_EVERY_N_MS(2000, system_service::emitMetrics());
        delay(20);
    }
}
