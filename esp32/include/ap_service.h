#pragma once

#include <event_bus/event_bus.h>
#include <settings/ap_settings.h>
#include <utils/timing.h>
#include <wifi/wifi_idf.h>
#include <wifi/dns_server.h>
#include <esp_timer.h>
#include <esp_http_server.h>
#include <string>

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

    esp_err_t getSettings(httpd_req_t *request);
    esp_err_t updateSettings(httpd_req_t *request, api_Request *protoReq);

  private:
    static constexpr const char *TAG = "APService";

    void onSettingsChanged(const api_APSettings &newSettings);

    APSettings _settings = APSettings_defaults();
    EventBus::Handle<api_APSettings> _settingsHandle;
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
