#pragma once

#include <template/stateful_service.h>
#include <template/stateful_proto_handler.h>
#include <template/stateful_persistence.h>
#include <settings/ap_settings.h>
#include <settings/app_network_settings.h>
#include <utils/timing.h>
#include <wifi/wifi_idf.h>
#include <wifi/dns_server.h>
#include <esp_timer.h>
#include <string>
#include <memory>

class APService : public StatefulService<APSettings> {
  public:
    APService();
    ~APService();

    void begin();
    void loop();
    void recoveryMode();

    void statusProto(api_APStatus &proto);
    APNetworkStatus getAPNetworkStatus();

    StatefulProtoHandler<APSettings, api_APSettings> protoHandler;

  private:
    FSPersistencePB<APSettings> _persistence;
    std::unique_ptr<DNSServer> _dnsServer;

    volatile unsigned long _lastManaged;
    volatile bool _reconfigureAp;
    volatile bool _recoveryMode = false;

    void reconfigureAP();
    void manageAP();
    void startAP();
    void stopAP();
    void handleDNS();
};
