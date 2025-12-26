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

#define CALLS_PER_SECOND_TIMED_START_TICK(name, function)                        \
    static uint64_t name##_##function##_start = 0;                           \
    static uint64_t name##_##function##_total_time = 0;                      \
    static uint64_t name##_##function##_call_count = 0;                      \
    name##_##function##_start = esp_timer_get_time();

#define CALLS_PER_SECOND_TIMED_END_TICK(name, function)                          \
    name##_##function##_total_time += (esp_timer_get_time() - name##_##function##_start); \
    name##_##function##_call_count++;

#define CALLS_PER_SECOND_TIMED_CALL(name, function, call)                        \
    static uint64_t name##_##function##_total_time = 0;                          \
    static uint64_t name##_##function##_call_count = 0;                          \
    do {                                                                         \
        uint64_t name##_##function##_start = esp_timer_get_time();               \
        call;                                                                    \
        name##_##function##_total_time += (esp_timer_get_time() - name##_##function##_start); \
        name##_##function##_call_count++;                                        \
    } while (0)

#define CALLS_PER_SECOND_TIMED_FUNC_PRINT(name, function)                        \
    if (name##_##function##_call_count > 0) {                                    \
        uint64_t avg = name##_##function##_total_time / name##_##function##_call_count; \
        if (avg < 1000) {                                                        \
            ESP_LOGI("Timing", "  %s: %llu us (avg over %llu calls)",            \
                     #function, avg, name##_##function##_call_count);            \
        } else {                                                                 \
            ESP_LOGI("Timing", "  %s: %llu ms (avg over %llu calls)",            \
                     #function, avg / 1000, name##_##function##_call_count);     \
        }                                                                        \
        name##_##function##_total_time = 0;                                      \
        name##_##function##_call_count = 0;                                      \
    }

#define CALLS_PER_SECOND_TIMED(name, ...)                                        \
    do {                                                                         \
        static uint64_t name##_last_print = 0;                                   \
        uint64_t name##_current_time = esp_timer_get_time() / 1000;              \
        if (name##_current_time - name##_last_print >= 1000) {                   \
            ESP_LOGI("Timing", "=== %s Average Timings ===", #name);             \
            __VA_ARGS__                                                          \
            name##_last_print = name##_current_time;                             \
        }                                                                        \
    } while (0)

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
