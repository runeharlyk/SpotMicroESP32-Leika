#pragma once

#include <esp_task_wdt.h>

// Stack size for the taskmgr's idle threads
#define IDLE_STACK_SIZE 2048
#define DEFAULT_STACK_SIZE 2048+512

class IdleTask
{
  private:

    float _idleRatio = 0;
    unsigned long _lastMeasurement;

    const int kMillisPerLoop = 1;
    const int kMillisPerCalc = 1000;

    unsigned long counter = 0;

  public:

    void ProcessIdleTime()
    {
        _lastMeasurement = millis();
        counter = 0;

        // We need to whack the watchdog so we delay in smalle bites until we've used up all the time

        while (true)
        {
            int delta = millis() - _lastMeasurement;
            if (delta >= kMillisPerCalc)
            {
                //Serial.printf("Core %u Spent %lu in delay during a window of %d for a ratio of %f\n",
                //  xPortGetCoreID(), counter, delta, (float)counter/delta);
                _idleRatio = ((float) counter  / delta);
                _lastMeasurement = millis();
                counter = 0;
            }
            else
            {
                esp_task_wdt_reset();
                delayMicroseconds(kMillisPerLoop*1000);
                counter += kMillisPerLoop;
            }
        }
    }

    // If idle time is spent elsewhere, it can be credited to this task.  Shouldn't add up to more time than actual though!

    void CountBonusIdleMillis(uint millis)
    {
        counter += millis;
    }

    IdleTask() : _lastMeasurement(millis())
    {
    }

    // GetCPUUsage
    //
    // Returns 100 less the amount of idle time that we were able to squander.

    float GetCPUUsage() const
    {
        // If the measurement failed to even get a chance to run, this core is maxed and there was no idle time

        if (millis() - _lastMeasurement > kMillisPerCalc)
            return 100.0f;

        // Otherwise, whatever cycles we were able to burn in the idle loop counts as "would have been idle" time
        return 100.0f-100*_idleRatio;
    }

    // Stub entry point for calling into it without a THIS pointer

    static void IdleTaskEntry(void * that)
    {
        IdleTask * pTask = (IdleTask *)that;
        pTask->ProcessIdleTime();
    }
};

// TaskManager
//
// TaskManager runs two tasks at just over idle priority that do nothing but try to burn CPU, and they
// keep track of how much they can burn.   It's assumed that everything else runs at a higher priority
// and thus they "starve" the idle tasks when doing work.

class TaskManager
{
    TaskHandle_t _hIdle0 = nullptr;
    TaskHandle_t _hIdle1 = nullptr;

    IdleTask _taskIdle0;
    IdleTask _taskIdle1;

  public:

    float GetCPUUsagePercent(int iCore = -1) const
    {
        if (iCore < 0)
            return (_taskIdle0.GetCPUUsage() + _taskIdle1.GetCPUUsage()) / 2;
        else if (iCore == 0)
            return _taskIdle0.GetCPUUsage();
        else if (iCore == 1)
            return _taskIdle1.GetCPUUsage();
        else
            throw new std::runtime_error("Invalid core passed to GetCPUUsagePercentCPU");
    }

    TaskManager() {}

    void begin()
    {
        Serial.printf("Replacing Idle Tasks with TaskManager...\n");
        // The idle tasks get created with a priority just ABOVE idle so that they steal idle time but nothing else.  They then
        // measure how much time is "wasted" at that lower priority and deem it to have been free CPU

        xTaskCreatePinnedToCore(_taskIdle0.IdleTaskEntry, "Idle0", IDLE_STACK_SIZE, &_taskIdle0, tskIDLE_PRIORITY + 1, &_hIdle0, 0);
        xTaskCreatePinnedToCore(_taskIdle1.IdleTaskEntry, "Idle1", IDLE_STACK_SIZE, &_taskIdle1, tskIDLE_PRIORITY + 1, &_hIdle1, 1);

        // We need to turn off the watchdogs because our idle measurement tasks burn all of the idle time just
        // to see how much there is (it's how they measure free CPU).  Thus, we starve the system's normal idle tasks
        // and have to feed the watchdog on our own.

        esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0));
        esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(1));
        esp_task_wdt_add(_hIdle0);
        esp_task_wdt_add(_hIdle1);
    }
};

void IRAM_ATTR NetworkHandlingLoopEntry(void *);
void IRAM_ATTR JSONWriterTaskEntry(void *);
void IRAM_ATTR MovementHandlingLoopEntry(void *);

#define DELETE_TASK(handle) if (handle != nullptr) vTaskDelete(handle)

class ESPTaskManager : public TaskManager
{
public:

private:
    TaskHandle_t _taskNetwork       = nullptr;
    TaskHandle_t _taskMovement      = nullptr;
    TaskHandle_t _taskJSONWriter    = nullptr;

public:

    ~ESPTaskManager()
    {
        DELETE_TASK(_taskNetwork);
        DELETE_TASK(_taskMovement);
        DELETE_TASK(_taskJSONWriter);
    }

    void StartThreads(){
        StartNetworkThread();
        StartMovementThread();
        StartJSONWriterThread();
    }

    void StartNetworkThread()
    {
        #if USE_WIFI
            log_i( ">> Launching Network Thread.  Mem: %u, LargestBlk: %u, PSRAM Free: %u/%u, ", ESP.getFreeHeap(),ESP.getMaxAllocHeap(), ESP.getFreePsram(), ESP.getPsramSize());
            xTaskCreatePinnedToCore(NetworkHandlingLoopEntry, "NetworkHandlingLoop", STACK_SIZE, nullptr, NET_PRIORITY, &_taskNetwork, NET_CORE);
        #endif
    }

    void StartMovementThread()
    {
        log_i(">> Launching Movement Thread");
        xTaskCreatePinnedToCore(MovementHandlingLoopEntry, "MovementHandlingLoop", STACK_SIZE, nullptr, MOVEMENT_PRIORITY, &_taskMovement, MOVEMENT_CORE);
    }

    void StartJSONWriterThread()
    {
        log_i(">> Launching JSON Writer Thread");
        xTaskCreatePinnedToCore(JSONWriterTaskEntry, "JSON Writer Loop", STACK_SIZE, nullptr, JSONWRITER_PRIORITY, &_taskJSONWriter, JSONWRITER_CORE);
    }

    void NotifyJSONWriterThread()
    {
        if (_taskJSONWriter == nullptr)
            return;

        log_w(">> Notifying JSON Writer Thread");
        // Wake up the writer invoker task if it's sleeping, or request another write cycle if it isn't
        xTaskNotifyGive(_taskJSONWriter);
    }

    void NotifyNetworkThread()
    {
        if (_taskNetwork == nullptr)
            return;

        //debugW(">> Notifying Network Thread");
        // Wake up the network task if it's sleeping, or request another read cycle if it isn't
        xTaskNotifyGive(_taskNetwork);
    }

    void NotifyMovementThread()
    {
        if (_taskMovement == nullptr)
            return;

        // Wake up the movement task if it's sleeping, or request another read cycle if it isn't
        xTaskNotifyGive(_taskMovement);
    }
};

extern ESPTaskManager g_TaskManager;