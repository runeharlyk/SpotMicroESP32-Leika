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

    feature_service::printFeatureConfiguration();

    ESPFS.begin(true);
#if FT_ENABLED(USE_WS2812)
    _ledService.loop();
#endif
    _wifiService.begin();

    setupServer();

    startServices();

    ESP_LOGV(TAG, "Starting misc loop task");
    xTaskCreatePinnedToCore(this->_loopImpl, "Spot misc", 4096, this, 2, NULL, APPLICATION_CORE);
}

void Spot::setupServer() {
    _server.config.max_uri_handlers = _numberEndpoints;
    _server.maxUploadSize = _maxFileUpload;
    _server.listen(_port);

    // WIFI
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

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", CORS_ORIGIN);
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Accept, Content-Type, Authorization");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Credentials", "true");
    DefaultHeaders::Instance().addHeader("Server", _appName);
}

void Spot::startServices() {
    _apService.begin();
    _peripherals.begin();
    _servoController.begin();
#if FT_ENABLED(USE_MOTION)
    _motionService.begin();
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
