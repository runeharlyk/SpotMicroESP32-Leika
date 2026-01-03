#pragma once

#include <communication/http_server.h>
#include <ESPmDNS.h>
#include <template/stateful_service.h>
#include <template/stateful_endpoint.h>
#include <template/stateful_persistence.h>
#include <settings/mdns_settings.h>
#include <utils/timing.h>
#include <string>

class MDNSService : public StatefulService<MDNSSettings> {
  private:
    FSPersistence<MDNSSettings, socket_message_MDNSSettingsData> _persistence;
    bool _started {false};

    void reconfigureMDNS();
    void startMDNS();
    void stopMDNS();
    void addServices();

  public:
    MDNSService();
    ~MDNSService();

    void begin();

    esp_err_t getStatus(HttpRequest& request);
    void getStatus(socket_message_MDNSStatusData& proto);

    esp_err_t queryServices(HttpRequest& request);

    StatefulHttpEndpoint<MDNSSettings, socket_message_MDNSSettingsData> endpoint;
};
