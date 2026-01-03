#pragma once

#include <ESPmDNS.h>
#include <PsychicHttp.h>
#include <WiFi.h>
#include <filesystem.h>
#include <global.h>
#include "esp_timer.h"
#include <string>

namespace system_service {
esp_err_t handleReset(PsychicRequest *request);
esp_err_t handleRestart(PsychicRequest *request);
esp_err_t handleSleep(PsychicRequest *request);
esp_err_t getStatus(PsychicRequest *request);

void reset();
void restart();
void sleep();
void status(JsonObject &root);

const char *resetReason(esp_reset_reason_t reason);
} // namespace system_service