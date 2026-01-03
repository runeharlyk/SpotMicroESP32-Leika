#pragma once

#include <template/stateful_service.h>
#include <template/stateful_endpoint.h>
#include <template/stateful_persistence.h>
#include <communication/http_server.h>
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

    esp_err_t getStatus(HttpRequest& request);
    void status(socket_message_APStatusData& proto);
    APNetworkStatus getAPNetworkStatus();

    StatefulHttpEndpoint<APSettings, socket_message_APSettingsData> endpoint;

  private:
    FSPersistence<APSettings, socket_message_APSettingsData> _persistence;

    DNSServer* _dnsServer;

    volatile unsigned long _lastManaged;
    volatile boolean _reconfigureAp;
    volatile boolean _recoveryMode = false;

    void reconfigureAP();
    void manageAP();
    void startAP();
    void stopAP();
    void handleDNS();
};
