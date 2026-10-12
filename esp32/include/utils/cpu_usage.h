#pragma once

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <atomic>

/**
 * Each core's CPU usage over the window since the previous sample, from the run time of its idle task: busy is the
 * share of the window the idle task did not run. Needs CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS counting in esp_timer
 * microseconds. One task samples; any task reads the latest.
 */
class CpuUsage {
  public:
    static constexpr int CORES = configNUMBER_OF_CORES;

    void sample() {
        const int64_t now = esp_timer_get_time();
        uint32_t idle[CORES];
        for (int core = 0; core < CORES; core++) idle[core] = idleRunTime(core);
        if (_sampled && now > _sampledAtUs) {
            int32_t sum = 0;
            for (int core = 0; core < CORES; core++) {
                const int32_t busy = busyPercent(idle[core] - _idle[core], now - _sampledAtUs);
                _busy[core].store(busy);
                sum += busy;
            }
            _total.store(sum / CORES);
        }
        std::copy(idle, idle + CORES, _idle);
        _sampledAtUs = now;
        _sampled = true;
    }

    int32_t core(int index) const { return index < CORES ? _busy[index].load() : 0; }
    int32_t total() const { return _total.load(); }

  private:
    // The 32-bit counters wrap every 71 minutes; a difference over one window is still exact.
    static uint32_t idleRunTime(int core) {
        TaskStatus_t status;
        vTaskGetInfo(xTaskGetIdleTaskHandleForCore(core), &status, pdFALSE, eInvalid);
        return static_cast<uint32_t>(status.ulRunTimeCounter);
    }

    // The idle task's time is counted at context switches, so over a short window it can exceed the window.
    static int32_t busyPercent(uint32_t idleUs, int64_t elapsedUs) {
        const int64_t busyUs = std::max<int64_t>(elapsedUs - idleUs, 0);
        return static_cast<int32_t>((100 * busyUs + elapsedUs / 2) / elapsedUs);
    }

    bool _sampled = false;
    int64_t _sampledAtUs = 0;
    uint32_t _idle[CORES] = {};
    std::atomic<int32_t> _busy[CORES] = {};
    std::atomic<int32_t> _total {0};
};
