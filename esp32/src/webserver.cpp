#include <webserver.h>

static const char *TAG = "WebServer";

namespace spot {

WebServer::WebServer(PsychicHttpServer *server, WiFiService *wifiService, APService *apService, EventSocket *socket,
                     SystemService *systemService, NTPService *ntpService, camera::CameraService *cameraService)
    : _server(server),
      _wifiService(wifiService),
      _apService(apService),
      _socket(socket),
      _systemService(systemService),
      _ntpService(ntpService),
      _cameraService(cameraService) {}

WebServer::~WebServer() {}

void WebServer::begin() {
    _server->config.max_uri_handlers = _numberEndpoints;
    _server->maxUploadSize = _maxFileUpload;

    _server->listen(_port);

    // Static files
    _server->serveStatic("/api/config/", ESPFS, "/config/");

    // Static services handle
    _server->on("/api/features", HTTP_GET, _featureService.getFeatures);

    // WiFi
    _server->on("/api/wifi/scan", HTTP_GET, _wifiService->handleScan);
    _server->on("/api/wifi/networks", HTTP_GET,
                [this](PsychicRequest *request) { return _wifiService->getNetworks(request); });
    _server->on("/api/wifi/sta/status", HTTP_GET,
                [this](PsychicRequest *request) { return _wifiService->getNetworkStatus(request); });
    _server->on("/api/wifi/sta/settings", HTTP_GET,
                [this](PsychicRequest *request) { return _wifiService->endpoint.getState(request); });
    _server->on("/api/wifi/sta/settings", HTTP_POST, [this](PsychicRequest *request, JsonVariant &json) {
        return _wifiService->endpoint.handleStateUpdate(request, json);
    });

    // AP
    _server->on("/api/wifi/ap/status", HTTP_GET,
                [this](PsychicRequest *request) { return _apService->getStatus(request); });
    _server->on("/api/wifi/ap/settings", HTTP_GET,
                [this](PsychicRequest *request) { return _apService->endpoint.getState(request); });
    _server->on("/api/wifi/ap/settings", HTTP_POST, [this](PsychicRequest *request, JsonVariant &json) {
        return _apService->endpoint.handleStateUpdate(request, json);
    });

    // System
    _server->on("/api/system/reset", HTTP_POST, _systemService->handleReset);
    _server->on("/api/system/restart", HTTP_POST, _systemService->handleRestart);
    _server->on("/api/system/sleep", HTTP_POST, _systemService->handleSleep);
    _server->on("/api/system/status", HTTP_GET, _systemService->getStatus);
    _server->on("/api/system/metrics", HTTP_GET, _systemService->getMetrics);

    // NTP
    _server->on("/api/ntp/time", HTTP_POST, _ntpService->handleTime);
    _server->on("/api/ntp/status", HTTP_GET, _ntpService->getNTPStatus);
    _server->on("/api/ntp/settings", HTTP_GET,
                [this](PsychicRequest *request) { return _ntpService->endpoint.getState(request); });
    _server->on("/api/ntp/settings", HTTP_POST, [this](PsychicRequest *request, JsonVariant &json) {
        return _ntpService->endpoint.handleStateUpdate(request, json);
    });

    // Camera
    _server->on("/api/camera/still", HTTP_GET, _cameraService->still);
    _server->on("/api/camera/stream", HTTP_GET, _cameraService->stream);
    _server->on("/api/camera/settings", HTTP_GET,
                [this](PsychicRequest *request) { return _cameraService->endpoint.getState(request); });
    _server->on("/api/camera/settings", HTTP_POST, [this](PsychicRequest *request, JsonVariant &json) {
        return _cameraService->endpoint.handleStateUpdate(request, json);
    });

    // File controller
    _server->on("/api/files", HTTP_GET, _fileSystemService.getFiles);
    _server->on("/api/files/delete", HTTP_POST, _fileSystemService.handleDelete);
    _server->on("/api/files/upload/*", HTTP_POST, _fileSystemService.getUploadHandler());
    _server->on("/api/files/edit", HTTP_POST, _fileSystemService.handleEdit);

    // Auth
    // _server->on("/api/verifyAuthorization", HTTP_GET, _featureService.getFeatures);
    // _server->on("/api/signIn", HTTP_GET, _featureService.getFeatures);
    // _server->on("/api/securitySettings", HTTP_GET, _featureService.getFeatures);
    // _server->on("/api/generateToken", HTTP_GET, _featureService.getFeatures);

    // // Firmware
    // _server->on("/api/downloadUpdate", HTTP_GET, _featureService.getFeatures);
    // _server->on("/api/uploadFirmware", HTTP_GET, _featureService.getFeatures);

    // Peripherals
    // _server->on("/api/peripheral/settings", HTTP_GET, _featureService.getFeatures);

    // Socket
    _server->on("/ws/events", _socket->getHandler());

    addHeaders();

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

    ESP_LOGI(TAG, "Finished registering endpoints");
}

void WebServer::loop() {}

void WebServer::addHeaders() {
#ifdef ENABLE_CORS
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", CORS_ORIGIN);
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Accept, Content-Type, Authorization");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Credentials", "true");
    _server->onNotFound([](PsychicRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            return request->reply(204);
        }
        return request->reply(404);
    });
#endif
    DefaultHeaders::Instance().addHeader("Server", "Spot micro");
}

} // namespace spot
