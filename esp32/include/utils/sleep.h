#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_rom_sys.h>

/**
 * vTaskDelay(n) wakes on the n-th tick interrupt and the current tick is already under way, so it can return
 * a whole tick early, and pdMS_TO_TICKS rounds down: at the firmware's 100 Hz tick,
 * vTaskDelay(pdMS_TO_TICKS(5)) does not wait at all. These round up and add the tick in progress.
 */
constexpr TickType_t ticksCovering(uint32_t ms, uint32_t tickRateHz = configTICK_RATE_HZ) {
    return (ms * tickRateHz + 999) / 1000 + 1;
}

// A wait shorter than a tick spins, since sleeping would take up to two ticks.
inline void sleepAtLeastMs(uint32_t ms) {
    if (ms < portTICK_PERIOD_MS) {
        esp_rom_delay_us(ms * 1000);
    } else {
        vTaskDelay(ticksCovering(ms));
    }
}
