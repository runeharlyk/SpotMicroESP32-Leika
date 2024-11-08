#include <StatefulService.h>
#include <stateful_service_endpoint.h>
#include <FSPersistence.h>
#include <settings/ap_settings.h>
#include <Timing.h>
#include <WiFi.h>

class APService : public StatefulService<APSettings> {
  public:
    APService();
    ~APService();

    void begin();
    void loop();
    void recoveryMode();

    esp_err_t getStatus(PsychicRequest *request);
    void status(JsonObject &root);
    APNetworkStatus getAPNetworkStatus();

    StatefulHttpEndpoint<APSettings> endpoint;

  private:
    PsychicHttpServer *_server;
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