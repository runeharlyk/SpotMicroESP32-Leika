#pragma once

#include <list>
#include <SPI.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <utils/math_utils.h>

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
        if (!_msg.success) {
            ESP_LOGE("IMU", "MPU6050 connection test failed");
            return false;
        }
        devStatus = _imu.dmpInitialize();
        if (devStatus == 0) {
            _imu.setXGyroOffset(0);
            _imu.setYGyroOffset(0);
            _imu.setZGyroOffset(0);
            _imu.setXAccelOffset(0);
            _imu.setYAccelOffset(0);
            _imu.setZAccelOffset(0);

            _imu.setI2CMasterModeEnabled(false);
            _imu.setI2CBypassEnabled(true);
            _imu.setSleepEnabled(false);
            _imu.setRate(1);
            _imu.resetFIFO();
            _imu.setDMPEnabled(true);

            ESP_LOGI("IMU", "MPU6050 DMP initialized successfully");
        } else {
            ESP_LOGE("IMU", "DMP initialization failed (code %d)", devStatus);
            _msg.success = false;
        }
#endif
#if FT_ENABLED(USE_BNO055)
        _msg.success = _imu.begin();
        if (!_msg.success) {
            ESP_LOGE("IMU", "BNO055 connection test failed");
            return false;
        }
        _imu.setExtCrystalUse(true);
#endif
        return _msg.success;
    }

    bool update() override {
        if (!_msg.success) return false;
#if FT_ENABLED(USE_MPU6050)
        uint16_t fifoCount = _imu.getFIFOCount();
        uint8_t intStatus = _imu.getIntStatus();

        if (intStatus & 0x10) {
            _imu.resetFIFO();
            ESP_LOGW("IMU", "FIFO overflow, resetting");
            return false;
        }

        if (_imu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
            _imu.dmpGetQuaternion(&q, fifoBuffer);
            _imu.dmpGetGravity(&gravity, &q);
            _imu.dmpGetYawPitchRoll(_msg.rpy, &q, &gravity);
            return true;
        }
        return false;
#endif
#if FT_ENABLED(USE_BNO055)
        sensors_event_t event;
        _imu.getEvent(&event);
        _msg.rpy[0] = event.orientation.x;
        _msg.rpy[1] = event.orientation.y;
        _msg.rpy[2] = event.orientation.z;
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
        _imu.CalibrateGyro(6);
        _imu.CalibrateAccel(6);
        ESP_LOGI("IMU", "Calibration complete");
        return true;
#elif FT_ENABLED(USE_BNO055)
        if (!_msg.success) return false;
        ESP_LOGI("IMU", "Starting calibration...");
        adafruit_bno055_offsets_t offsets;
        bool result = _imu.getSensorOffsets(offsets);
        ESP_LOGI("IMU", "Calibration complete");
        return result;
#else
        return false;
#endif
    }

  private:
#if FT_ENABLED(USE_MPU6050)
    MPU6050 _imu;
    uint8_t devStatus {0};
    Quaternion q;
    uint8_t fifoBuffer[64];
    VectorFloat gravity;
#endif
#if FT_ENABLED(USE_BNO055)
    Adafruit_BNO055 _imu {55, 0x29};
#endif
};