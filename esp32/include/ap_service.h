#pragma once

#include <template/stateful_service.h>
#include <template/stateful_endpoint.h>
#include <template/stateful_proto_endpoint.h>
#include <template/stateful_persistence.h>
#include <settings/ap_settings.h>
#include <utils/timing.h>
#include <WiFi.h>
#include "esp_timer.h"
#include <string>

class APService : public StatefulService<APSettings> {
  public:
    APService();
    ~APService();

    void begin();
    void loop();
    void recoveryMode();

    esp_err_t getStatus(httpd_req_t *request);
    esp_err_t getStatusProto(httpd_req_t *request);
    void status(JsonObject &root);
    void statusProto(api_APStatus &proto);
    APNetworkStatus getAPNetworkStatus();

    StatefulHttpEndpoint<APSettings> endpoint;
    StatefulProtoEndpoint<APSettings, api_APSettings> protoEndpoint;

  private:
    FSPersistence<APSettings> _persistence;

    DNSServer *_dnsServer;

    volatile unsigned long _lastManaged;
    volatile boolean _reconfigureAp;
    volatile boolean _recoveryMode = false;

    void reconfigureAP();
    void manageAP();
    void startAP();
    void stopAP();
    void handleDNS();
};
