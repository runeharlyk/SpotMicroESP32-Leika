#pragma once

#include <utils/math_utils.h>
#include <features.h>
#include <peripherals/sensor.hpp>

#if FT_ENABLED(USE_ICM20948)
#include "ICM_20948.h" 
#endif

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
#endif
#if FT_ENABLED(USE_ICM20948)
    #if USE_ICM20948_SPIMODE > 0
        _imu.begin(CS_PIN, SPI_PORT);
    #else
        _imu.begin(Wire, 1, 0xFF);
    #endif
    if (_imu.status != ICM_20948_Stat_Ok){ return false; }
    
    _imu.setSampleMode((ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr), ICM_20948_Sample_Mode_Continuous);
    if (_imu.status != ICM_20948_Stat_Ok){ return false; }
    
    ICM_20948_fss_t myFSS;
    myFSS.a = gpm2;
    myFSS.g = dps250;
    _imu.setFullScale((ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr), myFSS);
    if (_imu.status != ICM_20948_Stat_Ok){ return false; }
    // TODO: Setup low pass filter config
    _imu.startupMagnetometer();
    if (_imu.status != ICM_20948_Stat_Ok){ return false; }
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
#if FT_ENABLED(USE_ICM20948)
        if (_imu.dataReady())
        {
            _imu.getAGMT();
            _msg.rpy[0] = _imu.magX();
            _msg.rpy[1] = _imu.magY();
            _msg.rpy[2] = _imu.magZ();
        }
#endif
#if FT_ENABLED(USE_BNO055)
#endif
#if FT_ENABLED(USE_ICM20948)
    #if FT_ENABLED(USE_ICM20948_SPIMODE) > 0
        #define SPI_PORT SPI // TODO in periphearals_seetings.h
        #define CS_PIN 2 
        ICM_20948_SPI _imu;
    #else
        //#define WIRE_PORT Wire 
        ICM_20948_I2C _imu;
    #endif
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
#if FT_ENABLED(USE_ICM20948)
    #if FT_ENABLED(USE_ICM20948_SPIMODE) > 0
        #define SPI_PORT SPI // TODO in periphearals_seetings.h
        #define CS_PIN 2 
        ICM_20948_SPI _imu;
    #else
        //#define WIRE_PORT Wire 
        ICM_20948_I2C _imu;
    #endif
#endif
};
