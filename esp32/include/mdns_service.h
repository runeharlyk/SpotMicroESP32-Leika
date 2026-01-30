#pragma once

#include <esp_http_server.h>
#include <ESPmDNS.h>
#include <template/stateful_service.h>
#include <template/stateful_proto_endpoint.h>
#include <template/stateful_persistence_pb.h>
#include <settings/mdns_settings.h>
#include <utils/timing.h>

class MDNSService : public StatefulService<MDNSSettings> {
  private:
    FSPersistencePB<MDNSSettings> _persistence;
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
    esp_err_t queryServices(httpd_req_t *request, api_Request *protoReq);

    StatefulProtoEndpoint<MDNSSettings, api_MDNSSettings> protoEndpoint;
};
