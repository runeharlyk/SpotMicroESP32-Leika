#pragma once

#include <esp_task_wdt.h>
#include <map>
#include <string>
#include <vector>
#include <Arduino.h>

#define IDLE_STACK_SIZE 2048
#define DEFAULT_STACK_SIZE 2048 + 512

#define DELETE_TASK(handle) \
    if (handle != nullptr) vTaskDelete(handle)

struct task_t {
    String name;
    TaskHandle_t handle;
    uint32_t stackSize;
    UBaseType_t priority;
    BaseType_t coreId;
    bool pinned;
    bool active; // blocked ('B'), ready ('R'), deleted ('D') or suspended ('S').
};

class IdleTask {
  private:
    float idle_ratio = 0.0;
    unsigned long last_measurement;
    const int kMillisPerLoop = 1;
    const int kMillisPerCalc = 1000;
    unsigned long counter = 0;

  public:
    IdleTask() : last_measurement(millis()) {}

    void ProcessIdleTime() {
        last_measurement = millis();
        counter = 0;

        for (;;) {
            unsigned long current_time = millis();
            unsigned long delta = current_time - last_measurement;
            if (delta >= kMillisPerCalc) {
                idle_ratio = static_cast<float>(counter) / delta;
                last_measurement = current_time;
                counter = 0;
            } else {
                esp_task_wdt_reset();
                delayMicroseconds(kMillisPerLoop * 1000);
                counter += kMillisPerLoop;
            }
        }
    }

    float GetCPUUsage() const {
        if (millis() - last_measurement > kMillisPerCalc) return 100.0f;

        return 100.0f - 100 * idle_ratio;
    }

    static void IdleTaskEntry(void *that) { static_cast<IdleTask *>(that)->ProcessIdleTime(); }
};

class TaskManager {
  private:
    std::map<const char *, task_t> _tasks;
    IdleTask _taskIdle0;
    IdleTask _taskIdle1;

    TaskHandle_t _hIdle0;
    TaskHandle_t _hIdle1;

  public:
    TaskManager() {}

    void begin() {
        createTask(IdleTask::IdleTaskEntry, "Idle Core 0", IDLE_STACK_SIZE, &_taskIdle0, 1, &_hIdle0, 0);
        createTask(IdleTask::IdleTaskEntry, "Idle Core 1", IDLE_STACK_SIZE, &_taskIdle1, 1, &_hIdle1, 1);
        esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0));
        esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(1));
        esp_task_wdt_add(_hIdle0);
        esp_task_wdt_add(_hIdle1);
    }

    std::vector<task_t> getTasks() {
        update();
        std::vector<task_t> tasks;
        for (auto const &task : _tasks) tasks.push_back(task.second);
        return tasks;
    }

    int getTaskCount() const { return _tasks.size(); }

    int getKernelTaskCount() const { return uxTaskGetNumberOfTasks(); }

    void update() {
        for (auto task = _tasks.begin(); task != _tasks.end();) {
            eTaskState state = eTaskGetState(task->second.handle);
            if (state == eDeleted) {
                task = _tasks.erase(task);
            } else {
                _tasks[task->first].priority = uxTaskPriorityGet(task->second.handle);
                _tasks[task->first].coreId = xTaskGetAffinity(task->second.handle);
                ++task;
            }
        }
    }

    float getCpuUsage(int iCore = -1) const {
        if (iCore == 0)
            return _taskIdle0.GetCPUUsage();
        else if (iCore == 1)
            return _taskIdle1.GetCPUUsage();
        return (_taskIdle0.GetCPUUsage() + _taskIdle1.GetCPUUsage()) / 2;
    }

    BaseType_t createTask(void (*taskFunction)(void *), const char *name, uint32_t stackSize = 2048,
                          void *params = nullptr, UBaseType_t priority = 2, TaskHandle_t *handle = nullptr,
                          BaseType_t coreId = -1) {
        TaskHandle_t localHandle;
        if (handle == nullptr) handle = &localHandle;
        BaseType_t res = coreId == -1
                             ? xTaskCreate(taskFunction, name, stackSize, params, tskIDLE_PRIORITY + priority, handle)
                             : xTaskCreatePinnedToCore(taskFunction, name, stackSize, params,
                                                       tskIDLE_PRIORITY + priority, handle, coreId);
        task_t task = {name, handle, stackSize, priority, coreId, coreId != -1, true};
        if (res == pdPASS) _tasks[name] = task;
        return res;
    }

    void suspendTask(const char *name) {
        if (_tasks.find(name) != _tasks.end()) {
            vTaskSuspend(_tasks[name].handle);
            _tasks[name].active = false;
        }
    }

    void resumeTask(const char *name) {
        if (_tasks.find(name) != _tasks.end()) {
            vTaskResume(_tasks[name].handle);
            _tasks[name].active = true;
        }
    }

    void notifyTask(const char *name, uint32_t notificationValue, eNotifyAction action = eSetValueWithOverwrite) {
        if (_tasks.find(name) != _tasks.end()) xTaskNotify(_tasks[name].handle, notificationValue, action);
    }

    void deleteTask(const char *name) {
        if (_tasks.find(name) != _tasks.end()) {
            vTaskDelete(_tasks[name].handle);
            _tasks.erase(name);
        }
    }
};

class CPUBurnerTask {
  private:
    float burn_ratio;
    const int kMillisPerCalc = 1000;

  public:
    CPUBurnerTask(float ratio) : burn_ratio(ratio) {}

    void BurnCPUTask() {
        unsigned long burn_time = burn_ratio * kMillisPerCalc;
        unsigned long idle_time = kMillisPerCalc - burn_time;

        while (true) {
            unsigned long start_time = millis();
            while ((millis() - start_time) < burn_time) {
                esp_task_wdt_reset();
            }

            delay(idle_time);
        }
    }

    static void CPUBurnerTaskEntry(void *instance) { static_cast<CPUBurnerTask *>(instance)->BurnCPUTask(); }

    void StartTask() { xTaskCreate(CPUBurnerTaskEntry, "CPUBurnerTask", 2048, this, tskIDLE_PRIORITY + 2, nullptr); }
};

extern TaskManager g_taskManager;