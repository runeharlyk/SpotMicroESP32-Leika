#pragma once
// Host stand-in for the FreeRTOS pieces the I2C bus uses.
#include <cstdint>
typedef uint32_t TickType_t;
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
