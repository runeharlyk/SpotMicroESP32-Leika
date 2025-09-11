#pragma once

#include <list>
#include <SPI.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <utils/math_utils.h>

#include <Adafruit_HMC5883_U.h>
#include <Adafruit_Sensor.h>

#include <peripherals/sensor.hpp>

struct MagnetometerMsg : public SensorMessageBase {
    float rpy[3] {0, 0, 0};
    float heading {-1};

    void toJson(JsonVariant v) const override {
        JsonArray arr = v.to<JsonArray>();
        arr.add(rpy[0]);
        arr.add(rpy[1]);
        arr.add(rpy[2]);
        arr.add(heading);
        arr.add(success);
    }

    void fromJson(JsonVariantConst v) override {
        JsonArrayConst arr = v.as<JsonArrayConst>();
        rpy[0] = arr[0] | 0.0f;
        rpy[1] = arr[1] | 0.0f;
        rpy[2] = arr[2] | 0.0f;
        heading = arr[3] | -1.0f;
        success = arr[4] | false;
    }

    friend void toJson(JsonVariant v, MagnetometerMsg const& a) { a.toJson(v); }
};

class Magnetometer : public SensorBase<MagnetometerMsg> {
  public:
    bool initialize() {
        msg.success = _mag.begin();
        return msg.success;
    }

    bool update() {
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

  private:
    Adafruit_HMC5883_Unified _mag {12345};
    MagnetometerMsg msg;
    const float declinationAngle = 0.22;
};