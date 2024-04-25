#pragma once

#include <esp_task_wdt.h>
#include <map>
#include <string>

#define IDLE_STACK_SIZE 2048
#define DEFAULT_STACK_SIZE 2048+512

#define DELETE_TASK(handle) if (handle != nullptr) vTaskDelete(handle)

class TaskManager
{
  private:
    std::map<std::string, TaskHandle_t> tasks;

    static void idleTask(void *pvParameters)
    {
        while (true)
        {
            vTaskDelay(pdMS_TO_TICKS(1000)); // Delay to simulate workload
        }
    }

  public:
    TaskManager()
    {
        xTaskCreatePinnedToCore(idleTask, "IdleTaskCore0", 1024, nullptr, 0, nullptr, 0);
        xTaskCreatePinnedToCore(idleTask, "IdleTaskCore1", 1024, nullptr, 0, nullptr, 1);
    }

    void createTask(const std::string &name, void (*taskFunction)(void *), void *params = nullptr,
                    uint32_t stackSize = 2048, UBaseType_t priority = 1)
    {
        TaskHandle_t handle;
        xTaskCreate(taskFunction, name.c_str(), stackSize, params, priority + 1, &handle);
        tasks[name] = handle;
    }

    void suspendTask(const std::string &name)
    {
        if (tasks.find(name) != tasks.end())
        {
            vTaskSuspend(tasks[name]);
        }
    }

    void resumeTask(const std::string &name)
    {
        if (tasks.find(name) != tasks.end())
        {
            vTaskResume(tasks[name]);
        }
    }

    void notifyTask(const std::string &name, uint32_t notificationValue, eNotifyAction action = eSetValueWithOverwrite)
    {
        if (tasks.find(name) != tasks.end())
        {
            xTaskNotify(tasks[name], notificationValue, action);
        }
    }

    void deleteTask(const std::string &name)
    {
        if (tasks.find(name) != tasks.end())
        {
            vTaskDelete(tasks[name]);
            tasks.erase(name);
        }
    }

    float getCpuUsage(uint8_t coreId)
    {
        if (coreId > 1)
            return 0.0; // ESP32 has only core 0 and 1
        return uxTaskGetSystemState(nullptr, 0, nullptr) * 100.0 / configTICK_RATE_HZ;
    }
};