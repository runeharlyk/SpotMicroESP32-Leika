#pragma once
// Host stand-in: FreeRTOS mutexes, recursive or not, on std::recursive_mutex.
#include <freertos/FreeRTOS.h>
#include <mutex>
typedef std::recursive_mutex *SemaphoreHandle_t;
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return new std::recursive_mutex(); }
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t mutex, TickType_t) {
    mutex->lock();
    return pdTRUE;
}
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t mutex) {
    mutex->unlock();
    return pdTRUE;
}
inline void vSemaphoreDelete(SemaphoreHandle_t mutex) { delete mutex; }
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::recursive_mutex(); }
inline int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t) {
    mutex->lock();
    return pdTRUE;
}
inline int xSemaphoreGive(SemaphoreHandle_t mutex) {
    mutex->unlock();
    return pdTRUE;
}
