/**
 *   ESP32 SvelteKit
 *
 *   A simple, secure and extensible framework for IoT projects for ESP32 platforms
 *   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
 *   https://github.com/theelims/ESP32-sveltekit
 *
 *   Copyright (C) 2018 - 2023 rjwats
 *   Copyright (C) 2024 theelims
 *   Copyright (C) 2024 runeharlyk
 *
 *   All Rights Reserved. This software may be modified and distributed under
 *   the terms of the LGPL v3 license. See the LICENSE file for details.
 **/

#include <ESP32SvelteKit.h>

ESP32SvelteKit::ESP32SvelteKit(PsychicHttpServer *server, unsigned int numberEndpoints)
    : _server(server),
      _numberEndpoints(numberEndpoints),
      _taskManager(),
      _featureService(server),
      _securitySettingsService(server, &ESPFS),
      _wifiSettingsService(server, &ESPFS, &_securitySettingsService, &_socket),
      _wifiScanner(server, &_securitySettingsService),
      _wifiStatus(server, &_securitySettingsService),
      _apSettingsService(server, &ESPFS, &_securitySettingsService),
      _apStatus(server, &_securitySettingsService, &_apSettingsService),
      _socket(server, &_securitySettingsService, AuthenticationPredicates::IS_AUTHENTICATED),
#if FT_ENABLED(FT_NTP)
      _ntpSettingsService(server, &ESPFS, &_securitySettingsService),
      _ntpStatus(server, &_securitySettingsService),
#endif
#if FT_ENABLED(FT_UPLOAD_FIRMWARE)
      _uploadFirmwareService(server, &_securitySettingsService),
#endif
#if FT_ENABLED(FT_DOWNLOAD_FIRMWARE)
      _downloadFirmwareService(server, &_securitySettingsService, &_socket, &_taskManager),
#endif
#if FT_ENABLED(FT_SECURITY)
      _authenticationService(server, &_securitySettingsService),
#endif
#if FT_ENABLED(FT_SLEEP)
      _sleepService(server, &_securitySettingsService),
#endif
#if FT_ENABLED(FT_BATTERY)
      _batteryService(&_peripherals, &_socket),
#endif
#if FT_ENABLED(FT_ANALYTICS)
      _analyticsService(&_socket, &_taskManager),
#endif
#if FT_ENABLED(FT_CAMERA)
      _cameraService(server, &_taskManager, &_securitySettingsService),
      _cameraSettingsService(server, &ESPFS, &_securitySettingsService, &_socket),
#endif
      _restartService(server, &_securitySettingsService),
      _factoryResetService(server, &ESPFS, &_securitySettingsService),
      _systemStatus(server, &_securitySettingsService),
      _fileExplorer(server, &_securitySettingsService),
      _servoController(server, &ESPFS, &_securitySettingsService, &_peripherals, &_socket),
#if FT_ENABLED(FT_MOTION)
      _motionService(_server, &_socket, &_securitySettingsService, &_servoController, &_taskManager),
#endif
#if FT_ENABLED(FT_WS2812)
      _ledService(&_taskManager),
#endif
      _peripherals(server, &ESPFS, &_securitySettingsService, &_socket) {
}

void ESP32SvelteKit::begin() {
    ESP_LOGV("ESP32SvelteKit", "Loading settings from files system");
    ESP_LOGI("Running Firmware Version: %s", APP_VERSION);
    ESPFS.begin(true);

    _wifiSettingsService.initWiFi();

    setupServer();

    setupMDNS();

    startServices();

    ESP_LOGV("ESP32SvelteKit", "Starting loop task");
    _taskManager.createTask(this->_loopImpl, "Spot main", 4096, this, 2, NULL, ESP32SVELTEKIT_RUNNING_CORE);
}

void ESP32SvelteKit::setupServer() {
    _server->config.max_uri_handlers = _numberEndpoints;
    _server->listen(80);

#ifdef EMBED_WWW
    ESP_LOGV("ESP32SvelteKit", "Registering routes from PROGMEM static resources");
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
        _server->on(uri.c_str(), HTTP_GET, handler);

        // Set default end-point for all non matching requests
        // this is easier than using webServer.onNotFound()
        if (uri.equals("/index.html")) {
            _server->defaultEndpoint->setHandler(handler);
        }
    });
#else
    // Serve static resources from /www/
    ESP_LOGV("ESP32SvelteKit", "Registering routes from FS /www/ static resources");
    _server->serveStatic("/_app/", ESPFS, "/www/_app/");
    _server->serveStatic("/favicon.png", ESPFS, "/www/favicon.png");
    //  Serving all other get requests with "/www/index.htm"
    _server->onNotFound([](PsychicRequest *request) {
        if (request->method() == HTTP_GET) {
            PsychicFileResponse response(request, ESPFS, "/www/index.html", "text/html");
            return response.send();
            // String url = "http://" + request->host() + "/index.html";
            // request->redirect(url.c_str());
        }
    });
#endif
#ifdef SERVE_CONFIG_FILES
    _server->serveStatic("/api/config/", ESPFS, "/config/");
#endif

#if defined(ENABLE_CORS)
    ESP_LOGV("ESP32SvelteKit", "Enabling CORS headers");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", CORS_ORIGIN);
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Accept, Content-Type, Authorization");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Credentials", "true");
#endif
    DefaultHeaders::Instance().addHeader("Server", _appName);
}

void ESP32SvelteKit::setupMDNS() {
    ESP_LOGV("ESP32SvelteKit", "Starting MDNS");
    MDNS.begin(_wifiSettingsService.getHostname().c_str());
    MDNS.setInstanceName(_appName);
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "Firmware Version", APP_VERSION);
}

void ESP32SvelteKit::startServices() {
    _apStatus.begin();
    _socket.begin();
    _apSettingsService.begin();
    _factoryResetService.begin();
    _featureService.begin();
    _restartService.begin();
    _systemStatus.begin();
    _wifiSettingsService.begin();
    _wifiScanner.begin();
    _wifiStatus.begin();

#if FT_ENABLED(FT_UPLOAD_FIRMWARE)
    _uploadFirmwareService.begin();
#endif
#if FT_ENABLED(FT_DOWNLOAD_FIRMWARE)
    _downloadFirmwareService.begin();
#endif
#if FT_ENABLED(FT_NTP)
    _ntpSettingsService.begin();
    _ntpStatus.begin();
#endif
#if FT_ENABLED(FT_SECURITY)
    _authenticationService.begin();
    _securitySettingsService.begin();
#endif
#if FT_ENABLED(FT_ANALYTICS)
    _analyticsService.begin();
#endif
#if FT_ENABLED(FT_SLEEP)
    _sleepService.begin();
#endif
#if FT_ENABLED(FT_BATTERY)
    _batteryService.begin();
#endif
    _taskManager.begin();
    _fileExplorer.begin();
    _peripherals.begin();
    _servoController.begin();
#if FT_ENABLED(FT_MOTION)
    _motionService.begin();
#endif
#if FT_ENABLED(FT_CAMERA)
    _cameraService.begin();
    _cameraSettingsService.begin();
#endif
#if FT_ENABLED(FT_WS2812)
    _ledService.begin();
#endif
}

void IRAM_ATTR ESP32SvelteKit::loop() {
    while (1) {
#if FT_ENABLED(FT_WS2812)
        _ledService.loop();
#endif
        _wifiSettingsService.loop();
        _apSettingsService.loop();
#if FT_ENABLED(FT_ANALYTICS)
        _analyticsService.loop();
#endif
#if FT_ENABLED(FT_BATTERY)
        _batteryService.loop();
#endif
        _peripherals.loop();
        delay(20);
    }
}
