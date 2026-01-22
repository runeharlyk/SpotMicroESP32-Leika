#pragma once

#include <esp_http_server.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <template/stateful_service.h>
#include <template/stateful_endpoint.h>
#include <template/stateful_persistence.h>
#include <settings/mdns_settings.h>
#include <utils/timing.h>
#include <string>

class MDNSService : public StatefulService<MDNSSettings> {
  private:
    FSPersistence<MDNSSettings> _persistence;
    bool _started {false};

    void reconfigureMDNS();
    void startMDNS();
    void stopMDNS();
    void addServices();

  public:
    MDNSService();
    ~MDNSService();

    void begin();

    esp_err_t getStatus(httpd_req_t *request);
    void getStatus(JsonVariant &root);

    static esp_err_t queryServices(httpd_req_t *request, JsonVariant &json);

    StatefulHttpEndpoint<MDNSSettings> endpoint;
};
