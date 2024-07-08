/**
 *   ESP32 SvelteKit
 *
 *   A simple, secure and extensible framework for IoT projects for ESP32 platforms
 *   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
 *   https://github.com/theelims/ESP32-sveltekit
 *
 *   Copyright (C) 2023 theelims
 *   Copyright (C) 2024 runeharlyk
 *
 *   All Rights Reserved. This software may be modified and distributed under
 *   the terms of the LGPL v3 license. See the LICENSE file for details.
 **/

#include <BatteryService.h>

BatteryService::BatteryService(Peripherals *peripherals, EventSocket *socket)
    : _peripherals(peripherals), _socket(socket) {}

void BatteryService::begin() {}

void BatteryService::batteryEvent() {
    JsonDocument doc;
    char message[64];
    doc["voltage"] = _voltage;
    doc["current"] = _current;
    serializeJson(doc, message);
    _socket->emit(EVENT_BATTERY, message);
}
