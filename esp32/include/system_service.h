#pragma once

#include <ESPmDNS.h>
#include <communication/http_server.h>
#include <WiFi.h>
#include <filesystem.h>
#include <global.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_littlefs.h>
#include <string>

#include "platform_shared/message.pb.h"

namespace system_service {
esp_err_t handleReset(HttpRequest& request);
esp_err_t handleRestart(HttpRequest& request);
esp_err_t handleSleep(HttpRequest& request);
esp_err_t getStatus(HttpRequest& request);

void reset();
void restart();
void sleep();
void getAnalytics(socket_message_AnalyticsData &analytics);
void getStaticSystemInformation(socket_message_StaticSystemInformation &info);

const char* resetReason(esp_reset_reason_t reason);
} // namespace system_service
