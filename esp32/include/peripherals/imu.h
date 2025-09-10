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

struct IMUAnglesMsg {
    float rpy[3] {0, 0, 0};
    float temperature {-1};
    bool success {false};

    friend void toJson(JsonVariant v, IMUAnglesMsg const& a) {
        JsonArray arr = v.to<JsonArray>();
        arr.add(a.rpy[0]);
        arr.add(a.rpy[1]);
        arr.add(a.rpy[2]);
        arr.add(a.temperature);
        arr.add(a.success);
    }

    void fromJson(JsonVariantConst o) {
        JsonArrayConst arr = o.as<JsonArrayConst>();
        rpy[0] = arr[0].as<float>();
        rpy[1] = arr[1].as<float>();
        rpy[2] = arr[2].as<float>();
        temperature = arr[3].as<float>();
        success = arr[4].as<bool>();
    }
};

class IMU {
  public:
    IMU()
#if FT_ENABLED(USE_BNO055)
        : _imu(55, 0x29)
#endif
    {
    }
    bool initialize() {
#if FT_ENABLED(USE_MPU6050)
        _imu.initialize();
        imuMsg.success = _imu.testConnection();
        if (!imuMsg.success) return false;
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
        imuMsg.success = _imu.begin();
        if (!imuMsg.success) {
            return false;
        }
        _imu.setExtCrystalUse(true);
#endif
        return true;
    }

    bool readIMU() {
        if (!imuMsg.success) return false;
#if FT_ENABLED(USE_MPU6050)
        if (_imu.dmpPacketAvailable()) {
            if (_imu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
                _imu.dmpGetQuaternion(&q, fifoBuffer);
                _imu.dmpGetGravity(&gravity, &q);
                _imu.dmpGetYawPitchRoll(imuMsg.rpy, &q, &gravity);
                return true;
            }
        }
        return false;
#endif
#if FT_ENABLED(USE_BNO055)
        sensors_event_t event;
        _imu.getEvent(&event);
        imuMsg.rpy[0] = event.orientation.x;
        imuMsg.rpy[1] = event.orientation.y;
        imuMsg.rpy[2] = event.orientation.z;
#endif
        return true;
    }

    float getTemperature() { return imuMsg.temperature; }

    float getAngleX() { return imuMsg.rpy[2]; }

    float getAngleY() { return imuMsg.rpy[1]; }

    float getAngleZ() { return imuMsg.rpy[0]; }

    bool isActive() { return imuMsg.success; }

    IMUAnglesMsg getIMUAngles() { return imuMsg; }

    void readIMU(JsonObject& root) {
        if (!imuMsg.success) return;
        root["x"] = round2(getAngleX());
        root["y"] = round2(getAngleY());
        root["z"] = round2(getAngleZ());
    }

  private:
#if FT_ENABLED(USE_MPU6050)
    MPU6050 _imu;
    uint8_t devStatus {false};
    Quaternion q;
    uint8_t fifoBuffer[64];
    VectorFloat gravity;
#endif
#if FT_ENABLED(USE_BNO055)
    Adafruit_BNO055 _imu;
#endif
    IMUAnglesMsg imuMsg;
};