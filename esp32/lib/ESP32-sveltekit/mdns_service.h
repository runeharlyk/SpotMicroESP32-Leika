#pragma once

#include <PsychicHttp.h>
#include <ESPmDNS.h>
#include <template/stateful_service.h>
#include <template/stateful_endpoint.h>
#include <template/stateful_persistence.h>
#include <settings/mdns_settings.h>
#include <utils/timing.h>

class MDNSService : public StatefulService<MDNSSettings> {
  private:
    FSPersistence<MDNSSettings> _persistence;
    bool _started;

    void reconfigureMDNS();
    void startMDNS();
    void stopMDNS();
    void addServices();

  public:
    MDNSService();
    ~MDNSService();

    void begin();

    esp_err_t getStatus(PsychicRequest *request);
    void getStatus(JsonObject &root);

    static esp_err_t queryServices(PsychicRequest *request, JsonVariant &json);

    StatefulHttpEndpoint<MDNSSettings> endpoint;
};