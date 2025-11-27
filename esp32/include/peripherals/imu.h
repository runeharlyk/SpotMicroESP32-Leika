#pragma once

#include <list>
#include <SPI.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <utils/math_utils.h>

#if FT_ENABLED(USE_ICM20948)
#include "ICM_20948.h" 
#endif

#if FT_ENABLED(USE_MPU6050)
#include <MPU6050_6Axis_MotionApps612.h>
#endif

#if FT_ENABLED(USE_BNO055)
#include <Adafruit_BNO055.h>
#endif

#include <peripherals/sensor.hpp>

struct IMUAnglesMsg : public SensorMessageBase {
    float rpy[3] {0, 0, 0};
    float temperature {-1};
    bool success {false};

    void toJson(JsonVariant v) const override {
        JsonArray arr = v.to<JsonArray>();
        arr.add(rpy[0]);
        arr.add(rpy[1]);
        arr.add(rpy[2]);
        arr.add(temperature);
        arr.add(success);
    }

    void fromJson(JsonVariantConst v) override {
        JsonArrayConst arr = v.as<JsonArrayConst>();
        rpy[0] = arr[0] | -1.0f;
        rpy[1] = arr[1] | -1.0f;
        rpy[2] = arr[2] | -1.0f;
        temperature = arr[3] | -1.0f;
        success = arr[4] | false;
    }

    friend void toJson(JsonVariant v, IMUAnglesMsg const& a) { a.toJson(v); }
};

class IMU : public SensorBase<IMUAnglesMsg> {
  public:
    bool initialize() override {
#if FT_ENABLED(USE_MPU6050)
        _imu.initialize();
        _msg.success = _imu.testConnection();
        if (!_msg.success) return false;
        devStatus = _imu.dmpInitialize();
        if (devStatus == 0) {
            _imu.setDMPEnabled(false);
            _imu.setDMPConfig1(0x03);
            _imu.setDMPEnabled(true);
            _imu.setI2CMasterModeEnabled(false);
            _imu.setI2CBypassEnabled(true);
            _imu.setSleepEnabled(false);
        } else {
            return false;
        }
#endif
#if FT_ENABLED(USE_BNO055)
        _msg.success = _imu.begin();
        if (!_msg.success) {
            return false;
        }
        _imu.setExtCrystalUse(true);
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
#endif
        return true;
    }

    bool update() override {
        if (!_msg.success) return false;
#if FT_ENABLED(USE_MPU6050)
        if (_imu.dmpPacketAvailable()) {
            if (_imu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
                _imu.dmpGetQuaternion(&q, fifoBuffer);
                _imu.dmpGetGravity(&gravity, &q);
                _imu.dmpGetYawPitchRoll(_msg.rpy, &q, &gravity);
                return true;
            }
        }
        return false;
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
        sensors_event_t event;
        _imu.getEvent(&event);
        _msg.rpy[0] = event.orientation.x;
        _msg.rpy[1] = event.orientation.y;
        _msg.rpy[2] = event.orientation.z;
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
        return true;
    }

    float getTemperature() { return _msg.temperature; }

    float getAngleX() { return _msg.rpy[2]; }

    float getAngleY() { return _msg.rpy[1]; }

    float getAngleZ() { return _msg.rpy[0]; }

  private:
#if FT_ENABLED(USE_MPU6050)
    MPU6050 _imu;
    uint8_t devStatus {false};
    Quaternion q;
    uint8_t fifoBuffer[64];
    VectorFloat gravity;
#endif
#if FT_ENABLED(USE_BNO055)
    Adafruit_BNO055 _imu {55, 0x29};
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