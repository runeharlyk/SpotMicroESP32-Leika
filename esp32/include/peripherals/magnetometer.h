#pragma once

#include <SPI.h>
#include <Wire.h>
#include <utils/math_utils.h>

#include <Adafruit_HMC5883_U.h>
#include <Adafruit_Sensor.h>

#include <peripherals/sensor.hpp>

struct MagnetometerMsg {
    float rpy[3] {0, 0, 0};
    float heading {-1};
    bool success {false};
};

class Magnetometer : public SensorBase<MagnetometerMsg> {
  public:
    bool initialize() override {
        _msg.success = _mag.begin();
        return _msg.success;
    }

    bool update() override {
        if (!_msg.success) return false;
        sensors_event_t event;
        bool updated = _mag.getEvent(&event);
        if (!updated) return false;
        _msg.rpy[0] = event.magnetic.x;
        _msg.rpy[1] = event.magnetic.y;
        _msg.rpy[2] = event.magnetic.z;
        _msg.heading = atan2(event.magnetic.y, event.magnetic.x);
        _msg.heading += declinationAngle;
        if (_msg.heading < 0) _msg.heading += 2 * PI;
        if (_msg.heading > 2 * PI) _msg.heading -= 2 * PI;
        _msg.heading *= 180 / M_PI;
        return true;
    }

    float getMagX() { return _msg.rpy[0]; }

    float getMagY() { return _msg.rpy[1]; }

    float getMagZ() { return _msg.rpy[2]; }

    float getHeading() { return _msg.heading; }

  private:
    Adafruit_HMC5883_Unified _mag {12345};
    const float declinationAngle = 0.22;
};