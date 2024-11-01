#ifndef SYSTEM_SERVICE_H
#define SYSTEM_SERVICE_H

#include <ESPFS.h>
#include <ESPmDNS.h>
#include <PsychicHttp.h>
#include <WiFi.h>
#include <global.h>

namespace system_service {
esp_err_t handleReset(PsychicRequest *request);
esp_err_t handleRestart(PsychicRequest *request);
esp_err_t handleSleep(PsychicRequest *request);
esp_err_t getStatus(PsychicRequest *request);
esp_err_t getMetrics(PsychicRequest *request);

void reset();
void restart();
void sleep();
void status(JsonObject &root);
void metrics(JsonObject &root);
const char *resetReason(int reason);
} // namespace system_service

#endif