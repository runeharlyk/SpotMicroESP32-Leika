#include <domain/stateful_service_template.h>
#include <domain/stateful_service_endpoint.h>
#include <domain/stateful_service_persistence.h>
#include <domain/wifi/ap_settings.h>
#include <timing.h>
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

    HttpEndpoint<APSettings> endpoint;

  private:
    PsychicHttpServer *_server;
    FSPersistence<APSettings> _fsPersistence;

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