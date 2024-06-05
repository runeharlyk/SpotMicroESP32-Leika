#pragma once

#include <Adafruit_BMP085_U.h>
#include <Adafruit_HMC5883_U.h>
#include <Adafruit_Sensor.h>
#include <ArduinoJson.h>
#include <EventSocket.h>
#include <MPU6050_6Axis_MotionApps612.h>

#define IMU_INTERVAL 500
#define MAX_ESP_IMU_SIZE 500
#define EVENT_IMU "imu"

class IMUService
{
public:
    IMUService(EventSocket *socket) 
    : 
#if FT_ENABLED(FT_MAG)
    _mag(12345),
#endif
#if FT_ENABLED(FT_BMP)
    _bmp(10085),
#endif
    _socket(socket)
    {};

    void begin()
    {
        _socket->registerEvent(EVENT_IMU);
#if FT_ENABLED(FT_IMU)
        _imu.initialize();
        imu_success = _imu.testConnection();
        devStatus = _imu.dmpInitialize();
        if(!imu_success) {
            ESP_LOGE("IMUService", "MPU initialize failed");
        }
        _imu.setDMPEnabled(true);
        _imu.setI2CMasterModeEnabled(false);
        _imu.setI2CBypassEnabled(true);
        _imu.setSleepEnabled(false);
#endif
#if FT_ENABLED(FT_MAG)
        mag_success = _mag.begin();
        if(!mag_success) {
            ESP_LOGE("IMUService", "MAG initialize failed");
        }
#endif
#if FT_ENABLED(FT_BMP)
        bmp_success = _bmp.begin();
        if(!bmp_success) {
            ESP_LOGE("IMUService", "BMP initialize failed");
        }
#endif
    };

#if FT_ENABLED(FT_IMU)
    bool isIMUSuccess() {
        return imu_success;
    }

    float getTemp() {
        return imu_success ? imu_temperature : -1;
    }

    float getAngleX() {
        return imu_success ? ypr[0] * 180/M_PI : -1;
    }

    float getAngleY() {
        return imu_success ? ypr[1] * 180/M_PI : -1;
    }

    float getAngleZ() {
        return imu_success ? ypr[2] * 180/M_PI : -1;
    }

    bool readIMU() {
        bool updated = imu_success && _imu.dmpGetCurrentFIFOPacket(fifoBuffer);
        _imu.dmpGetQuaternion(&q, fifoBuffer);
        _imu.dmpGetGravity(&gravity, &q);
        _imu.dmpGetYawPitchRoll(ypr, &q, &gravity);
        return updated;
    }
#endif

#if FT_ENABLED(FT_MAG)
    float getHeading() {
        sensors_event_t event; 
        _mag.getEvent(&event);
        float heading = atan2(event.magnetic.y, event.magnetic.x);
        float declinationAngle = 0.22;
        heading += declinationAngle;
        if(heading < 0) heading += 2 * PI;
        if(heading > 2 * PI) heading -= 2 * PI;
        return heading * 180/M_PI;
    }
#endif

#if FT_ENABLED(FT_BMP)
    bool isBMPSuccess() {
        return bmp_success;
    }

    float getAltitude() {
        sensors_event_t event;
        _bmp.getEvent(&event);
        float seaLevelPressure = SENSORS_PRESSURE_SEALEVELHPA;
        return bmp_success && event.pressure ? _bmp.pressureToAltitude(seaLevelPressure, event.pressure) : -1;
    }

    float getPressure() {
        sensors_event_t event;
        _bmp.getEvent(&event);
        return bmp_success && event.pressure ? event.pressure : -1;
    }

    float getTemperature() {
        float temperature;
        _bmp.getTemperature(&temperature);
        return bmp_success ? temperature : -1;
    }
#endif

    double round2(double value) {
        return (int)(value * 100 + 0.5) / 100.0;
    }

    void loop()
    {
        unsigned long currentMillis = millis();

        if (currentMillis - _lastUpdate >= _updateInterval)
        {
            _lastUpdate = currentMillis;
            updateImu();
        }
    };
    
protected:
    JsonDocument doc;

    void updateImu() {
        doc.clear();
        bool newData = false;
#if FT_ENABLED(FT_IMU)
        newData = imu_success && readIMU();
        if (imu_success) {
            doc["x"] = round2(getAngleX());
            doc["y"] = round2(getAngleY());
            doc["z"] = round2(getAngleZ());
        }
#endif
#if FT_ENABLED(FT_MAG)
        newData = newData || mag_success;
        if (mag_success) {
            doc["heading"] = round2(getHeading());
        }
#endif
#if FT_ENABLED(FT_BMP)
        newData = newData || bmp_success;
        if (bmp_success) {
            doc["pressure"] = round2(getPressure());
            doc["altitude"] = round2(getAltitude());
            doc["bmp_temp"] = round2(getTemperature());
        }
#endif
        if(newData) {
            serializeJson(doc, message);
            _socket->emit(EVENT_IMU, message);
        }
    }

private:
#if FT_ENABLED(FT_IMU)
    MPU6050 _imu;
    bool imu_success {false};
    uint8_t devStatus {false};
    Quaternion q;
    uint8_t fifoBuffer[64];
    VectorFloat gravity;
    float ypr[3];
    float imu_temperature {-1};
#endif
#if FT_ENABLED(FT_MAG)
    Adafruit_HMC5883_Unified _mag;
    bool mag_success {false};
#endif
#if FT_ENABLED(FT_BMP)
    Adafruit_BMP085_Unified _bmp; 
    bool bmp_success {false};
#endif
    EventSocket *_socket;
    unsigned long _lastUpdate {0};
    unsigned long _updateInterval {IMU_INTERVAL};
    char message[MAX_ESP_IMU_SIZE];
};
