#pragma once

#include <NewPing.h>
#include <mutex>
#include <vector>

#define USS_INTERVAL 500

#define MAX_DISTANCE 200

#define LEFT_TRIG 12
#define RIGHT_TRIG 13

class USSService
{
public:
    USSService(const std::vector<uint8_t>& triggerPins, unsigned int maxDistance = MAX_DISTANCE)
    : distances(triggerPins.size(), 0) {
        for (size_t i = 0; i < triggerPins.size(); i++) {
            sensors.emplace_back(triggerPins[i], triggerPins[i], maxDistance);
        }
    };

    void begin()
    {
        xTaskCreatePinnedToCore(this->_loopImpl, "USS Service", 4096, this, tskIDLE_PRIORITY, NULL, ESP32SVELTEKIT_RUNNING_CORE);
    };

    unsigned int getDistance(size_t index) {
        if (index >= distances.size()) return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        return distances[index];
    }

protected:
    static void _loopImpl(void *_this) { static_cast<USSService *>(_this)->_loop(); }
    void _loop()
    {
        const TickType_t xFrequency = pdMS_TO_TICKS(USS_INTERVAL);
        TickType_t xLastWakeTime = xTaskGetTickCount();
        while (1)
        {
            for (size_t i = 0; i < sensors.size(); i++) {
                std::lock_guard<std::mutex> lock(mutex_);
                distances[i] = sensors[i].ping_cm();
                vTaskDelay(50);
            }
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
        }
    };

private:
    std::vector<NewPing> sensors;
    std::vector<unsigned int> distances;
    std::mutex mutex_;
};
