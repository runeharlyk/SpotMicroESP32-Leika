#pragma once

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

#include <EventSocket.h>
#include <JsonUtils.h>
#include <Peripherals.h>

#define ADC_VOLTAGE 0
#define ADC_CURRENT 1
#define ADC_BUTTON 2

#define EVENT_BATTERY "battery"
#define BATTERY_INTERVAL 10000
#define BATTERY_CHECK_INTERVAL 1000

// #define CURRENT_FACTOR 0.185 // 5A
// #define CURRENT_FACTOR 0.100 // 20A
#define CURRENT_FACTOR 0.066 // 30A

#define VOLTAGE_THRESHOLD 6.4
#define CURRENT_THRESHOLD 5

class BatteryService
{
public:
    BatteryService(Peripherals *peripherals, EventSocket *socket);

    void begin();

    void loop() {
        unsigned long currentMillis = millis();

        if (!_lastUpdate || (currentMillis - _lastUpdate) >= BATTERY_CHECK_INTERVAL)
        {
            _lastUpdate = currentMillis;
            updateBattery();
        }
        if(!_lastEmit || (currentMillis - _lastEmit) >= BATTERY_INTERVAL)
        {
            _lastEmit = currentMillis;
            batteryEvent();
        }
    }

    void updateBattery()
    {
        _voltage = _peripherals->readADCVoltage(ADC_VOLTAGE);
        float voltage = _peripherals->readADCVoltage(ADC_CURRENT);
        _current = (voltage - 2.5) / CURRENT_FACTOR;
    }

    float getVoltage() {
        return _voltage;
    }

    float getCurrent() {
        return _current;
    }

private:
    void batteryEvent();
    EventSocket *_socket;
    Peripherals *_peripherals;

    unsigned long _lastUpdate;
    unsigned long _lastEmit;
    float _voltage = 0;
    float _current = 0;
};
