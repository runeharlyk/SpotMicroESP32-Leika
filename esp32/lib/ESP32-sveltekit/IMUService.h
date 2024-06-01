#pragma once

#include <MPU6050_light.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085_U.h>
#include <Adafruit_HMC5883_U.h>
#include <ArduinoJson.h>
#include <EventSocket.h>

#define IMU_INTERVAL 500
#define MAX_ESP_IMU_SIZE 500
#define EVENT_IMU "imu"

class IMUService
{
public:
    IMUService(EventSocket *socket) 
    : 
#if FT_ENABLED(FT_IMU)
    _imu(Wire),
#endif
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
        byte status = _imu.begin();
        imu_success = status == 0;
        if(status != 0) {
            ESP_LOGE("IMUService", "MPU initialize failed: %d", status);
        }
        calibrate();
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
        return imu_success ? _imu.getTemp() : -1;
    }

    float getAngleX() {
        return imu_success ? _imu.getAngleX() : -1;
    }

    float getAngleY() {
        return imu_success ? _imu.getAngleY() : -1;
    }

    float getAngleZ() {
        return imu_success ? _imu.getGyroZ() : -1;
    }

    void calibrate() {
        if (imu_success) {
            _imu.calcOffsets(true, true);
        }
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
#if FT_ENABLED(FT_IMU)
        if (imu_success){
            _imu.update();
            doc["x"] = round2(getAngleX());
            doc["y"] = round2(getAngleY());
            doc["z"] = round2(getAngleZ());
            doc["imu_temp"] = round2(getTemp());
        }
#endif
#if FT_ENABLED(FT_MAG)
    if (mag_success) {
        doc["heading"] = round2(getHeading());
    }
#endif
#if FT_ENABLED(FT_BMP)
    if (bmp_success) {
        doc["pressure"] = round2(getPressure());
        doc["altitude"] = round2(getAltitude());
        doc["bmp_temp"] = round2(getTemperature());
    }
#endif
        serializeJson(doc, message);
        _socket->emit(EVENT_IMU, message);
    }

private:
#if FT_ENABLED(FT_IMU)
    MPU6050 _imu;
    bool imu_success {false};
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
