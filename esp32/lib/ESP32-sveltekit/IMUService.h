#pragma once

#include <MPU6050_light.h>
#include <ArduinoJson.h>
#include <EventSocket.h>

#define IMU_INTERVAL 200
#define MAX_ESP_IMU_SIZE 250
#define EVENT_IMU "imu"

class IMUService
{
public:
    IMUService(EventSocket *socket) : _socket(socket), _imu(Wire) {};

    void begin()
    {
        byte status = _imu.begin();
        imu_success = status == 0;
        if(status != 0) {
            ESP_LOGE("IMUService", "MPU initialize failed: %d", status);
        }
        _socket->registerEvent(EVENT_IMU);
        calibrate();
    };

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
        return imu_success ? _imu.getAngleX() : -1;
    }

    float getAngleZ() {
        return imu_success ? _imu.getAngleZ() : -1;
    }

    void calibrate() {
        if (imu_success) {
            _imu.calcOffsets(true, true);
        }
    }

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
        _imu.update();
        doc.clear();
        doc["x"] = round2(getAngleX());
        doc["y"] = round2(getAngleY());
        doc["z"] = round2(getAngleZ());
        doc["temp"] = round2(getTemp());

        serializeJson(doc, message);
        _socket->emit(EVENT_IMU, message);
    }

private:
    MPU6050 _imu;
    EventSocket *_socket;
    unsigned long _lastUpdate {0};
    unsigned long _updateInterval {IMU_INTERVAL};
    bool imu_success {false};
    char message[MAX_ESP_IMU_SIZE];
};
