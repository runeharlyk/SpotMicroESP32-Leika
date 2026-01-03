#pragma once

#include <ESPmDNS.h>
#include <PsychicHttp.h>
#include <WiFi.h>
#include <filesystem.h>
#include <global.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_littlefs.h>
#include <string>

#include "platform_shared/message.pb.h"

namespace system_service {
esp_err_t handleReset(PsychicRequest *request);
esp_err_t handleRestart(PsychicRequest *request);
esp_err_t handleSleep(PsychicRequest *request);
esp_err_t getStatus(PsychicRequest *request);

void reset();
void restart();
void sleep();
void status(JsonObject &root);
void getAnalytics(socket_message_AnalyticsData &analytics);

const char *resetReason(esp_reset_reason_t reason);
} // namespace system_service