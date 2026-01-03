#pragma once

#include <SPI.h>
#include <Wire.h>
#include <utils/math_utils.h>

#include <Adafruit_BMP085_U.h>
#include <Adafruit_Sensor.h>

#include <peripherals/sensor.hpp>

struct BarometerMsg {
    float pressure {-1};
    float altitude {-1};
    float temperature {-1};
    bool success {false};
};

class Barometer : public SensorBase<BarometerMsg> {
  public:
    bool initialize() override {
        _msg.success = _bmp.begin();
        return _msg.success;
    }

    bool update() override {
        if (!_msg.success) return false;
        _bmp.getTemperature(&_msg.temperature);
        sensors_event_t event;
        _bmp.getEvent(&event);
        _msg.pressure = event.pressure;
        _msg.altitude = _bmp.pressureToAltitude(seaLevelPressure, _msg.pressure);
        return true;
    }

    float getPressure() { return _msg.pressure; }

    float getAltitude() { return _msg.altitude; }

    float getTemperature() { return _msg.temperature; }

    bool active() { return _msg.success; }

  private:
    Adafruit_BMP085_Unified _bmp {10085};

    const float seaLevelPressure = SENSORS_PRESSURE_SEALEVELHPA;
};