#pragma once

#include <list>
#include <SPI.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <utils/math_utils.h>

#include <Adafruit_BMP085_U.h>
#include <Adafruit_Sensor.h>

#include <peripherals/sensor.hpp>

struct BarometerMsg : public SensorMessageBase {
    float pressure {-1};
    float altitude {-1};
    float temperature {-1};
    bool success {false};

    void toJson(JsonVariant v) const override {
        JsonArray arr = v.to<JsonArray>();
        arr.add(pressure);
        arr.add(altitude);
        arr.add(temperature);
        arr.add(success);
    }

    void fromJson(JsonVariantConst v) override {
        JsonArrayConst arr = v.as<JsonArrayConst>();
        pressure = arr[0] | -1.0f;
        altitude = arr[1] | -1.0f;
        temperature = arr[2] | -1.0f;
        success = arr[3] | false;
    }

    friend void toJson(JsonVariant v, BarometerMsg const& a) { a.toJson(v); }
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