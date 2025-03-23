#pragma once

#include <list>
#include <SPI.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <utils/math_utils.h>

#include <MPU6050_6Axis_MotionApps612.h>

class IMU {
  public:
    IMU() {}
    bool initialize() {
#if FT_ENABLED(USE_MPU6050)
        _imu.initialize();
        imu_success = _imu.testConnection();
        devStatus = _imu.dmpInitialize();
        if (!imu_success) return false;
        _imu.setDMPEnabled(true);
        _imu.setI2CMasterModeEnabled(false);
        _imu.setI2CBypassEnabled(true);
        _imu.setSleepEnabled(false);
#endif
        return true;
    }

    bool readIMU() {
        if (!imu_success) return false;
#if FT_ENABLED(USE_MPU6050)
        bool updated = _imu.dmpGetCurrentFIFOPacket(fifoBuffer);
        _imu.dmpGetQuaternion(&q, fifoBuffer);
        _imu.dmpGetGravity(&gravity, &q);
        _imu.dmpGetYawPitchRoll(ypr, &q, &gravity);
        return updated;
#endif
    }

    float getTemperature() { return imu_success ? imu_temperature : -1; }

    float getAngleX() { return imu_success ? ypr[0] * 180 / M_PI : 0; }

    float getAngleY() { return imu_success ? ypr[1] * 180 / M_PI : 0; }

    float getAngleZ() { return imu_success ? ypr[2] * 180 / M_PI : 0; }

    void readIMU(JsonObject& root) {
        if (!imu_success) return;
        root["x"] = round2(getAngleX());
        root["y"] = round2(getAngleY());
        root["z"] = round2(getAngleZ());
    }

    bool active() { return imu_success; }

  private:
#if FT_ENABLED(USE_MPU6050)
    MPU6050 _imu;
    uint8_t devStatus {false};
    Quaternion q;
    uint8_t fifoBuffer[64];
    VectorFloat gravity;
#endif
    bool imu_success {false};
    float ypr[3];
    float imu_temperature {-1};
};