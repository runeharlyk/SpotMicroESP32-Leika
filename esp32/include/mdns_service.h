#pragma once

#include <esp_http_server.h>
#include <mdns.h>
#include <event_bus/event_bus.h>
#include <settings/mdns_settings.h>
#include <utils/timing.h>

class MDNSService {
  public:
    MDNSService();
    ~MDNSService();

    void begin();

    esp_err_t getStatus(httpd_req_t *request);
    esp_err_t queryServices(httpd_req_t *request, api_Request *protoReq);

    esp_err_t getSettings(httpd_req_t *request);
    esp_err_t updateSettings(httpd_req_t *request, api_Request *protoReq);

  private:
    static constexpr const char *TAG = "MDNSService";

    void onSettingsChanged(const api_MDNSSettings &newSettings);

    MDNSSettings _settings = MDNSSettings_defaults();
    EventBus::Handle<api_MDNSSettings> _settingsHandle;
    bool _started {false};

    void reconfigureMDNS();
    void startMDNS();
    void stopMDNS();
    void addServices();
};
