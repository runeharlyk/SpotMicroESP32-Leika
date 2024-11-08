#pragma once

#include <ArduinoJson.h>
#include <filesystem.h>
#include <event_socket.h>
#include <task_manager.h>
#include <WiFi.h>
#include <timing.h>

#define MAX_ESP_ANALYTICS_SIZE 2024
#define EVENT_ANALYTICS "analytics"
#define ANALYTICS_INTERVAL 2000

class AnalyticsService {
  public:
    AnalyticsService() {};

    void loop() { EXECUTE_EVERY_N_MS(ANALYTICS_INTERVAL, updateAnalytics()); };

  private:
    JsonDocument doc;
    char message[MAX_ESP_ANALYTICS_SIZE];

    void updateAnalytics() {
        if (!socket.hasSubscribers(EVENT_ANALYTICS)) return;
        doc.clear();
        doc["uptime"] = millis() / 1000;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["total_heap"] = ESP.getHeapSize();
        doc["min_free_heap"] = ESP.getMinFreeHeap();
        doc["max_alloc_heap"] = ESP.getMaxAllocHeap();
        doc["fs_used"] = ESPFS.usedBytes();
        doc["fs_total"] = ESPFS.totalBytes();
        doc["core_temp"] = temperatureRead();
        doc["cpu0_usage"] = g_taskManager.getCpuUsage(0);
        doc["cpu1_usage"] = g_taskManager.getCpuUsage(1);
        doc["cpu_usage"] = g_taskManager.getCpuUsage();
        JsonArray tasks = doc["tasks"].to<JsonArray>();
        for (auto const &task : g_taskManager.getTasks()) {
            JsonObject nested = tasks.add<JsonObject>();
            nested["name"] = task.name;
            nested["stackSize"] = task.stackSize;
            nested["priority"] = task.priority;
            nested["coreId"] = task.coreId;
        }

        serializeJson(doc, message);
        socket.emit(EVENT_ANALYTICS, message);
    }
};
