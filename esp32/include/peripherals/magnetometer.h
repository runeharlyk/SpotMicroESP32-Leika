#pragma once

#include <list>
#include <SPI.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <utils/math_utils.h>

#include <Adafruit_HMC5883_U.h>
#include <Adafruit_Sensor.h>

struct MagnetometerMsg {
    float rpy[3] {0, 0, 0};
    float heading {-1};
    bool success {false};

    friend void toJson(JsonVariant v, MagnetometerMsg const& a) {
        JsonArray arr = v.to<JsonArray>();
        arr.add(a.rpy[0]);
        arr.add(a.rpy[1]);
        arr.add(a.rpy[2]);
        arr.add(a.heading);
        arr.add(a.success);
    }

    void fromJson(JsonVariantConst o) {
        JsonArrayConst arr = o.as<JsonArrayConst>();
        rpy[0] = arr[0].as<float>();
        rpy[1] = arr[1].as<float>();
        rpy[2] = arr[2].as<float>();
        heading = arr[3].as<float>();
        success = arr[4].as<bool>();
    }
};

class Magnetometer {
  public:
    Magnetometer() : _mag(12345) {}

    bool initialize() {
        msg.success = _mag.begin();
        return msg.success;
    }

    bool readMagnetometer() {
        if (!msg.success) return false;
        sensors_event_t event;
        bool updated = _mag.getEvent(&event);
        if (!updated) return false;
        msg.rpy[0] = event.magnetic.x;
        msg.rpy[1] = event.magnetic.y;
        msg.rpy[2] = event.magnetic.z;
        msg.heading = atan2(event.magnetic.y, event.magnetic.x);
        msg.heading += declinationAngle;
        if (msg.heading < 0) msg.heading += 2 * PI;
        if (msg.heading > 2 * PI) msg.heading -= 2 * PI;
        msg.heading *= 180 / M_PI;
        return true;
    }

    float getMagX() { return msg.rpy[0]; }

    float getMagY() { return msg.rpy[1]; }

    float getMagZ() { return msg.rpy[2]; }

    float getHeading() { return msg.heading; }

    MagnetometerMsg getMagnetometerMsg() { return msg; }

    void readMagnetometer(JsonObject& root) {
        if (!msg.success) return;
        root["heading"] = round2(getHeading());
    }

    bool isActive() { return msg.success; }

  private:
    Adafruit_HMC5883_Unified _mag;
    MagnetometerMsg msg;
    const float declinationAngle = 0.22;
};