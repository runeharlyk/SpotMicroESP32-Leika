#pragma once

#include <esp_http_server.h>
#include <mdns.h>
#include <eventbus.hpp>
#include <settings/mdns_settings.h>
#include <utils/timing.h>

class WebServer;

class MDNSService {
  public:
    MDNSService();
    ~MDNSService();

    void begin();

    esp_err_t getStatus(httpd_req_t *request);
    esp_err_t queryServices(httpd_req_t *request, api_Request *protoReq);

    void registerRoutes(WebServer &server);

  private:
    MDNSSettings _settings {};
    SubscriptionHandle _settingsHandle;
    bool _started {false};

    void reconfigureMDNS();
    void startMDNS();
    void stopMDNS();
    void addServices();
};
