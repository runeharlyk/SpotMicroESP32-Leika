#pragma once

/**
 *   ESP32 SvelteKit
 *
 *   A simple, secure and extensible framework for IoT projects for ESP32 platforms
 *   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
 *   https://github.com/theelims/ESP32-sveltekit
 *
 *   Copyright (C) 2023 theelims
 *
 *   All Rights Reserved. This software may be modified and distributed under
 *   the terms of the LGPL v3 license. See the LICENSE file for details.
 **/

#include <ArduinoJson.h>
#include <filesystem.h>
#include <EventSocket.h>
#include <TaskManager.h>
#include <WiFi.h>
#include <Timing.h>

#define MAX_ESP_ANALYTICS_SIZE 2024
#define EVENT_ANALYTICS "analytics"
#define ANALYTICS_INTERVAL 2000

class AnalyticsService {
  public:
    AnalyticsService(EventSocket *socket, TaskManager *taskManager) : _socket(socket), _taskManager(taskManager) {};

    void begin() {};

    void loop() { EXECUTE_EVERY_N_MS(ANALYTICS_INTERVAL, updateAnalytics()); };
    JsonDocument doc;
    char message[MAX_ESP_ANALYTICS_SIZE];

  private:
    EventSocket *_socket;
    TaskManager *_taskManager;

    void updateAnalytics() {
        if (!_socket->hasSubscribers(EVENT_ANALYTICS)) return;
        doc.clear();
        doc["uptime"] = millis() / 1000;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["total_heap"] = ESP.getHeapSize();
        doc["min_free_heap"] = ESP.getMinFreeHeap();
        doc["max_alloc_heap"] = ESP.getMaxAllocHeap();
        doc["fs_used"] = ESPFS.usedBytes();
        doc["fs_total"] = ESPFS.totalBytes();
        doc["core_temp"] = temperatureRead();
        doc["cpu0_usage"] = _taskManager->getCpuUsage(0);
        doc["cpu1_usage"] = _taskManager->getCpuUsage(1);
        doc["cpu_usage"] = _taskManager->getCpuUsage();
        JsonArray tasks = doc["tasks"].to<JsonArray>();
        for (auto const &task : _taskManager->getTasks()) {
            JsonObject nested = tasks.add<JsonObject>();
            nested["name"] = task.name;
            nested["stackSize"] = task.stackSize;
            nested["priority"] = task.priority;
            nested["coreId"] = task.coreId;
        }

        serializeJson(doc, message);
        _socket->emit(EVENT_ANALYTICS, message);
    }
};
