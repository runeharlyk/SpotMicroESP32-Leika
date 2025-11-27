#pragma once

#include <utils/math_utils.h>
#include <peripherals/sensor.hpp>
#include <peripherals/drivers/bmp180.h>

struct BarometerMsg {
    float pressure {-1};
    float altitude {-1};
    float temperature {-1};
    bool success {false};
};

class Barometer : public SensorBase<BarometerMsg> {
  public:
    bool initialize(void* _) override {
        _msg.success = _bmp.begin();
        if (_msg.success) {
            ESP_LOGI("BMP", "BMP180 initialized successfully");
        } else {
            ESP_LOGE("BMP", "BMP180 initialization failed");
        }
        return _msg.success;
    }

    bool update() override {
        if (!_msg.success) return false;
        if (!_bmp.update()) return false;
        _msg.temperature = _bmp.getTemperature();
        _msg.pressure = _bmp.getPressure();
        _msg.altitude = _bmp.getAltitude();
        return true;
    }

    float getPressure() { return _msg.pressure; }
    float getAltitude() { return _msg.altitude; }
    float getTemperature() { return _msg.temperature; }
    bool active() { return _msg.success; }

  private:
    BMP180Driver _bmp;
};
