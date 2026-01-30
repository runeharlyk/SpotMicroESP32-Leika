#pragma once

#include "esp_timer.h"

#define CONCAT(a, b) a##b

#define UNIQUE_VAR(base) CONCAT(base, __LINE__)

#define EXECUTE_EVERY_N_MS(n, code)                                                               \
    do {                                                                                          \
        static volatile uint64_t UNIQUE_VAR(lastExecution_) = 0;                                  \
        uint64_t currentMillis = esp_timer_get_time() / 1000;                                     \
        if (UNIQUE_VAR(lastExecution_) == 0 || currentMillis - UNIQUE_VAR(lastExecution_) >= n) { \
            code;                                                                                 \
            UNIQUE_VAR(lastExecution_) = currentMillis;                                           \
        }                                                                                         \
    } while (0)

#define TIME_IT(code)                                                                       \
    {                                                                                       \
        uint64_t time_it_start = esp_timer_get_time();                                      \
        code;                                                                               \
        uint64_t time_it_elapsed = esp_timer_get_time() - time_it_start;                    \
        if (time_it_elapsed < 1000) {                                                       \
            ESP_LOGI("Time It", "Time elapsed: %llu microseconds", time_it_elapsed);        \
        } else if (time_it_elapsed < 1000000) {                                             \
            ESP_LOGI("Time It", "Time elapsed: %llu milliseconds", time_it_elapsed / 1000); \
        } else {                                                                            \
            ESP_LOGI("Time It", "Time elapsed: %.2f seconds", time_it_elapsed / 1000000.0); \
        }                                                                                   \
    }

#define CALLS_PER_SECOND(name)                                               \
    static uint64_t name##_count = 0;                                        \
    static uint64_t last_time = 0;                                           \
    name##_count++;                                                          \
    if (esp_timer_get_time() / 1000 - last_time >= 1000) {                   \
        ESP_LOGI("Calls", "%s: %llu calls per second", #name, name##_count); \
        name##_count = 0;                                                    \
        last_time = esp_timer_get_time() / 1000;                             \
    }

#define WARN_IF_SLOW(name, period_ms)                                                                    \
    static uint64_t name##_slow_count = 0;                                                               \
    static uint64_t name##_slow_last_time = 0;                                                           \
    name##_slow_count++;                                                                                 \
    if (esp_timer_get_time() / 1000 - name##_slow_last_time >= 1000) {                                   \
        uint64_t expected_hz = 1000 / (period_ms);                                                       \
        if (name##_slow_count < expected_hz) {                                                           \
            ESP_LOGW("Timing", "%s: %llu Hz (expected %llu Hz)", #name, name##_slow_count, expected_hz); \
        }                                                                                                \
        name##_slow_count = 0;                                                                           \
        name##_slow_last_time = esp_timer_get_time() / 1000;                                             \
    }
