#pragma once

#include <template/stateful_service.h>
#include <template/stateful_proto_endpoint.h>
#include <template/stateful_persistence_pb.h>
#include <settings/ap_settings.h>
#include <utils/timing.h>
#include <WiFi.h>
#include "esp_timer.h"

class APService : public StatefulService<APSettings> {
  public:
    APService();
    ~APService();

    void begin();
    void loop();
    void recoveryMode();

    esp_err_t getStatusProto(httpd_req_t *request);
    void statusProto(api_APStatus &proto);
    APNetworkStatus getAPNetworkStatus();

    StatefulProtoEndpoint<APSettings, api_APSettings> protoEndpoint;

  private:
    FSPersistencePB<APSettings> _persistence;
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
