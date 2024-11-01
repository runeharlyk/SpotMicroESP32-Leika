#ifndef APSettingsConfig_h
#define APSettingsConfig_h

#include <filesystem.h>
#include <stateful_persistence.h>
#include <utilities/json_utilities.h>
#include <WiFi.h>
#include <stateful_endpoint.h>

#include <DNSServer.h>
#include <IPAddress.h>

#include <domain/ap_settings.h>
#include <timing.h>

class APService : public StatefulService<APSettings> {
  public:
    APService();

    void begin();
    void loop();
    APNetworkStatus getAPNetworkStatus();
    void recoveryMode();

    esp_err_t getStatus(PsychicRequest *request);

    HttpEndpoint<APSettings> endpoint;

  private:
    PsychicHttpServer *_server;
    FSPersistence<APSettings> _fsPersistence;

    // for the captive portal
    DNSServer *_dnsServer;

    // for the mangement delay loop
    volatile unsigned long _lastManaged;
    volatile boolean _reconfigureAp;
    volatile boolean _recoveryMode = false;

    void reconfigureAP();
    void manageAP();
    void startAP();
    void stopAP();
    void handleDNS();
};

#endif // end APSettingsConfig_h