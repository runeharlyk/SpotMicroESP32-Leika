#pragma once

#include <MPU6050_light.h>

#define IMU_INTERVAL 2000

class IMUService
{
public:
    IMUService():_imu(Wire){};

    void begin()
    {
        byte status = _imu.begin();
        if(status != 0) {
            ESP_LOGE("IMUService", "MPU initialize failed");
            vTaskDelete(NULL);
            return;
        }
        xTaskCreatePinnedToCore(this->_loopImpl, "IMU Service", 4096, this, tskIDLE_PRIORITY, NULL, ESP32SVELTEKIT_RUNNING_CORE);
    };

    float getTemp() {
        return _imu.getTemp();
    }

    float getAngleX() {
        return _imu.getAngleX();
    }

    float getAngleY() {
        return _imu.getAngleX();
    }

    float getAngleZ() {
        return _imu.getAngleZ();
    }

protected:
    static void _loopImpl(void *_this) { static_cast<IMUService *>(_this)->_loop(); }
    void _loop()
    {
        vTaskDelay(100);
        _imu.calcOffsets(true,true);
        TickType_t xLastWakeTime = xTaskGetTickCount();
        while (1)
        {
            _imu.update();
            vTaskDelayUntil(&xLastWakeTime, IMU_INTERVAL / portTICK_PERIOD_MS);
        }
    };

private:
    MPU6050 _imu;
};
