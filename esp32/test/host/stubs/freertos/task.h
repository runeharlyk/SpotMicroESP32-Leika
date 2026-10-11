#pragma once
// Host stand-in for the FreeRTOS task API the firmware's host-tested headers use: vTaskDelay counts the ticks slept
// instead of sleeping, and each core's idle task has a run time the test sets.
#include <freertos/FreeRTOS.h>
#include <cstdint>

namespace fake_rtos {
inline TickType_t delayedTicks = 0;
// Microseconds each core's idle task has run, in the 32-bit counter the firmware's configuration keeps.
inline uint32_t idleRunTimeUs[configNUMBER_OF_CORES] = {};
} // namespace fake_rtos

inline void vTaskDelay(TickType_t ticks) { fake_rtos::delayedTicks += ticks; }

struct FakeTask {
    int core;
};
typedef FakeTask *TaskHandle_t;
typedef enum { eRunning, eReady, eBlocked, eSuspended, eDeleted, eInvalid } eTaskState;
struct TaskStatus_t {
    uint32_t ulRunTimeCounter;
};

inline TaskHandle_t xTaskGetIdleTaskHandleForCore(BaseType_t core) {
    static FakeTask idle[configNUMBER_OF_CORES] = {{0}, {1}};
    return &idle[core];
}

inline void vTaskGetInfo(TaskHandle_t task, TaskStatus_t *status, BaseType_t, eTaskState) {
    status->ulRunTimeCounter = fake_rtos::idleRunTimeUs[task->core];
}
