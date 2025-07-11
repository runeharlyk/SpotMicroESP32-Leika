#pragma once

#include <list>
#include <SPI.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <utils/math_utils.h>

#include <Adafruit_HMC5883_U.h>
#include <Adafruit_Sensor.h>

class Magnetometer {
  public:
    Magnetometer() : _mag(12345) {}

    bool initialize() {
        mag_success = _mag.begin();
        return mag_success;
    }

    bool readMagnetometer() {
        if (!mag_success) return false;
        sensors_event_t event;
        bool updated = _mag.getEvent(&event);
        if (!updated) return false;
        ypr[0] = event.magnetic.x;
        ypr[1] = event.magnetic.y;
        ypr[2] = event.magnetic.z;
        heading = atan2(event.magnetic.y, event.magnetic.x);
        heading += declinationAngle;
        if (heading < 0) heading += 2 * PI;
        if (heading > 2 * PI) heading -= 2 * PI;
        heading *= 180 / M_PI;
        return true;
    }

    float getMagX() { return mag_success ? ypr[0] : 0; }

    float getMagY() { return mag_success ? ypr[1] : 0; }

    float getMagZ() { return mag_success ? ypr[2] : 0; }

    float getHeading() { return heading; }

    void readMagnetometer(JsonObject& root) {
        if (!mag_success) return;
        root["heading"] = round2(getHeading());
    }

    bool active() { return mag_success; }

  private:
    Adafruit_HMC5883_Unified _mag;
    bool mag_success {false};
    float ypr[3];
    float heading {0};

    const float declinationAngle = 0.22;
};