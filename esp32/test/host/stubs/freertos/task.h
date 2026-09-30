#pragma once
// Host stand-in for vTaskDelay: counts the ticks slept instead of sleeping.
#include <freertos/FreeRTOS.h>
namespace fake_rtos {
inline TickType_t delayedTicks = 0;
}
inline void vTaskDelay(TickType_t ticks) { fake_rtos::delayedTicks += ticks; }
