#pragma once

#include <PsychicHttp.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <task_manager.h>
#include <ESPFS.h>
#include <global.h>

class SystemService {
  private:
  public:
    SystemService();
    ~SystemService();

    static esp_err_t handleReset(PsychicRequest *request);
    static esp_err_t handleRestart(PsychicRequest *request);
    static esp_err_t handleSleep(PsychicRequest *request);
    static esp_err_t getStatus(PsychicRequest *request);
    static esp_err_t getMetrics(PsychicRequest *request);

    static void reset();
    static void restart();
    static void sleep();
    static void status(JsonObject &root);
    static void metrics(JsonObject &root);
    static const char *resetReason(int reason);
};
