#ifndef TIMING_H
#define TIMING_H

#define CONCAT(a, b) a##b

#define UNIQUE_VAR(base) CONCAT(base, __LINE__)

#define EXECUTE_EVERY_N_MS(n, code)                                                               \
    do {                                                                                          \
        static volatile unsigned long UNIQUE_VAR(lastExecution_) = 0;                             \
        unsigned long currentMillis = millis();                                                   \
        if (UNIQUE_VAR(lastExecution_) == 0 || currentMillis - UNIQUE_VAR(lastExecution_) >= n) { \
            code;                                                                                 \
            UNIQUE_VAR(lastExecution_) = currentMillis;                                           \
        }                                                                                         \
    } while (0)

#define TIME_IT(code)                                                                       \
    {                                                                                       \
        uint32_t time_it_start = micros();                                                  \
        code;                                                                               \
        uint32_t time_it_elapsed = micros() - time_it_start;                                \
        if (time_it_elapsed < 1000) {                                                       \
            ESP_LOGI("Time It", "Time elapsed: %lu microseconds", time_it_elapsed);         \
        } else if (time_it_elapsed < 1000000) {                                             \
            ESP_LOGI("Time It", "Time elapsed: %lu milliseconds", time_it_elapsed / 1000);  \
        } else {                                                                            \
            ESP_LOGI("Time It", "Time elapsed: %.2f seconds", time_it_elapsed / 1000000.0); \
        }                                                                                   \
    }

#endif