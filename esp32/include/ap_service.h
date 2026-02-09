#pragma once

#include <esp_http_server.h>
#include <eventbus.hpp>
#include <settings/ap_settings.h>
#include <utils/timing.h>
#include <wifi/wifi_idf.h>
#include <wifi/dns_server.h>
#include <esp_timer.h>
#include <string>

class WebServer;

class APService {
  public:
    APService();
    ~APService();

    void begin();
    void loop();
    void recoveryMode();

    esp_err_t getStatusProto(httpd_req_t *request);
    void statusProto(api_APStatus &proto);
    APNetworkStatus getAPNetworkStatus();

    void registerRoutes(WebServer &server);

  private:
    APSettings _settings {};
    SubscriptionHandle _settingsHandle;
    DNSServer *_dnsServer;

    volatile unsigned long _lastManaged;
    volatile bool _reconfigureAp;
    volatile bool _recoveryMode = false;

    void reconfigureAP();
    void manageAP();
    void startAP();
    void stopAP();
    void handleDNS();
};
