#pragma once

#include <mdns.h>
#include <esp_http_server.h>
#include <utils/http_utils.h>
#include <string>

namespace mdns_service {

void begin(const char *hostname);
void end();
void addService(const char *service, const char *proto, uint16_t port);
void addServiceTxt(const char *service, const char *proto, const char *key, const char *value);

} // namespace mdns_service
