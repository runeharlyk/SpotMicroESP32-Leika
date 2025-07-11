#pragma once

#include <list>
#include <SPI.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <utils/math_utils.h>

#include <Adafruit_BMP085_U.h>
#include <Adafruit_Sensor.h>

class Barometer {
  public:
    Barometer() : _bmp(10085) {}

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

    void readBarometer(JsonObject& root) {
        if (!bmp_success) return;
        root["pressure"] = round2(getPressure());
        root["altitude"] = round2(getAltitude());
        root["bmp_temp"] = round2(getTemperature());
    }

    bool active() { return bmp_success; }

  private:
    Adafruit_BMP085_Unified _bmp;
    bool bmp_success {false};
    float pressure {0};
    float altitude {0};
    float temperature {0};

    const float seaLevelPressure = SENSORS_PRESSURE_SEALEVELHPA;
};