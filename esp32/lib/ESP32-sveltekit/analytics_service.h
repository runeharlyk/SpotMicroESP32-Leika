#pragma once

#include <ArduinoJson.h>
#include <filesystem.h>
#include <event_socket.h>
#include <system_service.h>
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
        JsonObject root = doc.to<JsonObject>();
        system_service::metrics(root);
        serializeJson(doc, message);
        socket.emit(EVENT_ANALYTICS, message);
    }
};
