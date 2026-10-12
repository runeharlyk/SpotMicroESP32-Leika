#pragma once

#include <mdns.h>
#include <wifi/wifi_idf.h>
#include <filesystem.h>
#include <global.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_littlefs.h>
#include <string>

#include "platform_shared/message.pb.h"

namespace system_service {
void reset();
void restart();
// Measures each core's CPU usage since the previous call; one task calls it, every two seconds.
void sampleCpuUsage();
void getAnalytics(socket_message_AnalyticsData &analytics);
void getStaticSystemInformation(socket_message_StaticSystemInformation &info);

const char *resetReason(esp_reset_reason_t reason);
} // namespace system_service
