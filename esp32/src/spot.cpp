#include <spot.h>

TaskManager g_task_manager;

namespace spot {

Spot::Spot(PsychicHttpServer *server)
    : _server(server),
      _webserver(server, &_wifiService, &_apService, &_socket, &_systemService, &_ntpService, &_cameraService),
      _socket(server),
      _ntpService(server),
      _servoController(server, &ESPFS, &_peripherals, &_socket),
      _peripherals(server, &_socket),
      _motionService(server, &_socket, &_servoController) {}

Spot::~Spot() {}

void Spot::beginAsync() {
    begin();
    g_task_manager.createTask(this->_loopImpl, "Spot main", 4096, this, 2, NULL);
}

void Spot::begin() {
    ESPFS.begin(true);
    g_task_manager.begin();
    startServices();
}

void Spot::startServices() {
    _wifiService.begin();
    _wifiService.setupMDNS(name);
    _apService.begin();
    _ntpService.begin();
    _cameraService.begin();
    _socket.begin();
    _webserver.begin();
    _ledService.begin();
    _motionService.begin();
    _peripherals.begin();
    _servoController.begin();
    // _batteryService.begin();
    //_authenticationService.begin();
    //_firmwareService.begin();
}

void IRAM_ATTR Spot::_loop() {
    while (true) {
        loop();
        delay(20);
    }
}

void IRAM_ATTR Spot::loop() {
    _webserver.loop();
    _wifiService.loop();
    _apService.loop();
    _ledService.loop();
    // _batteryService.loop();
    _peripherals.loop();

    EXECUTE_EVERY_N_MS(500, {
        if (!_socket.hasSubscribers("analytics")) return;
        doc.clear();
        JsonObject jsonObject = doc.to<JsonObject>();
        _systemService.metrics(jsonObject);
        serializeJson(doc, message);
        _socket.emit("analytics", message);
    });
}

} // namespace spot