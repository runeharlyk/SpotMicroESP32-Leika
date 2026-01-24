#pragma once

#include <utils/math_utils.h>
#include <peripherals/sensor.hpp>
#include <peripherals/drivers/hmc5883l.h>

struct MagnetometerMsg {
    float rpy[3] {0, 0, 0};
    float heading {-1};
    bool success {false};
};

class Magnetometer : public SensorBase<MagnetometerMsg> {
  public:
    bool initialize() override {
        _msg.success = _mag.begin();
        if (_msg.success) {
            ESP_LOGI("MAG", "HMC5883L initialized successfully");
        } else {
            ESP_LOGE("MAG", "HMC5883L initialization failed");
        }
        return _msg.success;
    }

    bool update() override {
        if (!_msg.success) return false;
        if (!_mag.update()) return false;
        _msg.rpy[0] = _mag.getMagX();
        _msg.rpy[1] = _mag.getMagY();
        _msg.rpy[2] = _mag.getMagZ();
        _msg.heading = _mag.getHeading();
        return true;
    }

    float getMagX() { return _msg.rpy[0]; }
    float getMagY() { return _msg.rpy[1]; }
    float getMagZ() { return _msg.rpy[2]; }
    float getHeading() { return _msg.heading; }

  private:
    HMC5883LDriver _mag;
};
