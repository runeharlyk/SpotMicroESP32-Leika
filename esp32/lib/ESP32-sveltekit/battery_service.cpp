#include <battery_service.h>

BatteryService::BatteryService(Peripherals *peripherals) : _peripherals(peripherals) {}

void BatteryService::begin() {}

void BatteryService::batteryEvent() {
    JsonDocument doc;
    char message[64];
    doc["voltage"] = _voltage;
    doc["current"] = _current;
    serializeJson(doc, message);
    socket.emit(EVENT_BATTERY, message);
}
