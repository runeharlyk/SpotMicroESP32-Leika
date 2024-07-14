#ifndef TIMING_H
#define TIMING_H

#define CONCAT(a, b) a##b

#define UNIQUE_VAR(base) CONCAT(base, __LINE__)

#define EXECUTE_EVERY_N_MS(n, code)                                           \
    do {                                                                      \
        static volatile unsigned long UNIQUE_VAR(lastExecution_) = ULONG_MAX; \
        unsigned long currentMillis = millis();                               \
        if (currentMillis - UNIQUE_VAR(lastExecution_) >= n) {                \
            code;                                                             \
            UNIQUE_VAR(lastExecution_) = currentMillis;                       \
        }                                                                     \
    } while (0)

#endif