#pragma once

#include <esp_http_server.h>
#include <wifi/wifi_idf.h>
#include <mdns.h>
#include <string>

#include <filesystem.h>
#include <utils/timing.h>
#include <eventbus.hpp>
#include <settings/wifi_settings.h>

class WebServer;

#define WIFI_EVENT_STA_DISCONNECTED_IDF WIFI_EVENT_STA_DISCONNECTED
#define WIFI_EVENT_STA_STOP_IDF WIFI_EVENT_STA_STOP
#define IP_EVENT_STA_GOT_IP_IDF 1000

class WiFiService {
  public:
    WiFiService();
    ~WiFiService();

    void begin();
    void loop();

    void setupMDNS(const char *hostname);
    void selectNetwork(uint32_t index);

    const char *getHostname() { return _settings.hostname; }

    static esp_err_t handleScan(httpd_req_t *request);
    static esp_err_t getNetworks(httpd_req_t *request);
    static esp_err_t getNetworkStatus(httpd_req_t *request);

    void registerRoutes(WebServer &server);

  private:
    void onStationModeDisconnected(int32_t event, void *event_data);
    void onStationModeStop(int32_t event, void *event_data);
    static void onStationModeGotIP(int32_t event, void *event_data);

    WiFiSettings _settings {};
    SubscriptionHandle _settingsHandle;

    void reconfigureWiFiConnection();
    void manageSTA();
    void configureNetwork(WiFiNetwork &network);

    unsigned long _lastConnectionAttempt;
    bool _stopping;

    constexpr static uint16_t reconnectDelay {10000};
};
