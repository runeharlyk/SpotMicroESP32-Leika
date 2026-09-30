#pragma once
// Host stand-in for the FreeRTOS pieces the firmware's host-tested headers use, at the firmware's 100 Hz tick.
#include <cstdint>
typedef uint32_t TickType_t;
#define configTICK_RATE_HZ 100
#define portTICK_PERIOD_MS (1000 / configTICK_RATE_HZ)
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) ((TickType_t)((uint64_t)(ms) * configTICK_RATE_HZ / 1000))
#define pdTRUE 1
