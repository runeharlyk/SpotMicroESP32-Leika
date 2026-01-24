#pragma once

#include <utils/math_utils.h>
#include <features.h>
#include <peripherals/sensor.hpp>

#if FT_ENABLED(USE_MPU6050)
#include <peripherals/drivers/mpu6050.h>
#endif

#if FT_ENABLED(USE_BNO055)
#include <peripherals/drivers/bno055.h>
#endif

struct IMUAnglesMsg {
    float rpy[3] {0, 0, 0};
    float temperature {-1};
    bool success {false};
};

class IMU : public SensorBase<IMUAnglesMsg> {
  public:
    bool initialize() override {
#if FT_ENABLED(USE_MPU6050)
        _msg.success = _imu.begin();
        if (!_msg.success) {
            ESP_LOGE("IMU", "MPU6050 initialization failed");
            return false;
        }
        ESP_LOGI("IMU", "MPU6050 initialized successfully");
#endif
#if FT_ENABLED(USE_BNO055)
        _msg.success = _imu.begin();
        if (!_msg.success) {
            ESP_LOGE("IMU", "BNO055 initialization failed");
            return false;
        }
        ESP_LOGI("IMU", "BNO055 initialized successfully");
#endif
        return _msg.success;
    }

    bool update() override {
        if (!_msg.success) return false;
#if FT_ENABLED(USE_MPU6050)
        if (!_imu.update()) return false;
        _msg.rpy[0] = _imu.getYaw();
        _msg.rpy[1] = _imu.getPitch();
        _msg.rpy[2] = _imu.getRoll();
        _msg.temperature = _imu.getTemperature();
#endif
#if FT_ENABLED(USE_BNO055)
        if (!_imu.update()) return false;
        _msg.rpy[0] = _imu.getHeading();
        _msg.rpy[1] = _imu.getPitch();
        _msg.rpy[2] = _imu.getRoll();
#endif
        return true;
    }

    float getTemperature() { return _msg.temperature; }
    float getAngleX() { return _msg.rpy[2]; }
    float getAngleY() { return _msg.rpy[1]; }
    float getAngleZ() { return _msg.rpy[0]; }

    bool calibrate() {
#if FT_ENABLED(USE_MPU6050)
        if (!_msg.success) return false;
        ESP_LOGI("IMU", "Starting calibration...");
        bool result = _imu.calibrate();
        ESP_LOGI("IMU", "Calibration complete");
        return result;
#elif FT_ENABLED(USE_BNO055)
        if (!_msg.success) return false;
        ESP_LOGI("IMU", "Starting calibration...");
        bool result = _imu.calibrate();
        ESP_LOGI("IMU", "Calibration complete");
        return result;
#else
        return false;
#endif
    }

  private:
#if FT_ENABLED(USE_MPU6050)
    MPU6050Driver _imu;
#endif
#if FT_ENABLED(USE_BNO055)
    BNO055Driver _imu;
#endif
};
