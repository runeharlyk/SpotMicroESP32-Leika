#pragma once

#include <ESPmDNS.h>
#include <esp_http_server.h>
#include <WiFi.h>
#include <communication/websocket_adapter.h>
#include <filesystem.h>
#include <global.h>
#include <esp_timer.h>
#include <utils/http_utils.h>
#include <string>

#define MAX_ESP_ANALYTICS_SIZE 2024
#define EVENT_ANALYTICS "analytics"

namespace system_service {
esp_err_t handleReset(httpd_req_t *req);
esp_err_t handleRestart(httpd_req_t *req);
esp_err_t handleSleep(httpd_req_t *req);
esp_err_t getStatus(httpd_req_t *req);
esp_err_t getMetrics(httpd_req_t *req);

void reset();
void restart();
void sleep();
void status(JsonObject &root);
void metrics(JsonObject &root);

void emitMetrics(Websocket &socket);

const char *resetReason(esp_reset_reason_t reason);
} // namespace system_service