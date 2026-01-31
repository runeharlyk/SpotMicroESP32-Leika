#pragma once

#include <mdns.h>
#include <esp_http_server.h>
#include <wifi/wifi_idf.h>
#include <filesystem.h>
#include <global.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_littlefs.h>
#include <string>

#include "platform_shared/message.pb.h"

namespace system_service {
esp_err_t handleReset(httpd_req_t *request);
esp_err_t handleRestart(httpd_req_t *request);
esp_err_t handleSleep(httpd_req_t *request);

void reset();
void restart();
void sleep();
void status(JsonObject &root);
void getAnalytics(socket_message_AnalyticsData &analytics);
void getStaticSystemInformation(socket_message_StaticSystemInformation &info);

const char *resetReason(esp_reset_reason_t reason);
} // namespace system_service
