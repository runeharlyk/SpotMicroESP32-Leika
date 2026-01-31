#pragma once

#include <esp_http_server.h>
#include <wifi/wifi_idf.h>
#include <mdns.h>
#include <string>

#include <event_bus/event_bus.h>
#include <settings/wifi_settings.h>
#include <utils/timing.h>

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

    const char *getHostname() {
        static api_WifiSettings cached_settings;
        EventBus::peek(cached_settings);
        return cached_settings.hostname;
    }

    static esp_err_t handleScan(httpd_req_t *request);
    static esp_err_t getNetworks(httpd_req_t *request);
    static esp_err_t getNetworkStatus(httpd_req_t *request);

  private:
    void onStationModeDisconnected(int32_t event, void *event_data);
    void onStationModeStop(int32_t event, void *event_data);
    static void onStationModeGotIP(int32_t event, void *event_data);

    void onSettingsChanged(const api_WifiSettings &newSettings);

    void reconfigureWiFiConnection();
    void manageSTA();
    void configureNetwork(const WiFiNetwork &network);

    api_WifiSettings getSettings() const {
        api_WifiSettings settings;
        EventBus::peek(settings);
        return settings;
    }

    EventBus::Handle<api_WifiSettings> _settingsHandle;

    bool _initialized;
    unsigned long _lastConnectionAttempt;
    bool _stopping;

    constexpr static uint16_t reconnectDelay {10000};
    static constexpr const char *TAG = "WiFiService";
};
