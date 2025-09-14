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

class Barometer {
  public:
    bool initialize() {
        bmp_success = _bmp.begin();
        return bmp_success;
    }

    bool readBarometer() {
        if (!bmp_success) return false;
        _bmp.getTemperature(&temperature);
        sensors_event_t event;
        _bmp.getEvent(&event);
        pressure = event.pressure;
        altitude = _bmp.pressureToAltitude(seaLevelPressure, pressure);
        return true;
    }

    float getPressure() { return pressure; }

    float getAltitude() { return altitude; }

    float getTemperature() { return temperature; }

    bool active() { return bmp_success; }

  private:
    Adafruit_BMP085_Unified _bmp {10085};
    bool bmp_success {false};
    float pressure {0};
    float altitude {0};
    float temperature {0};

    const float seaLevelPressure = SENSORS_PRESSURE_SEALEVELHPA;
};