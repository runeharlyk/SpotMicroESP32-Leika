#pragma once

#include <communication/http_server.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <string>

#include <filesystem.h>
#include <utils/timing.h>
#include <template/stateful_service.h>
#include <template/stateful_persistence.h>
#include <template/stateful_endpoint.h>
#include <settings/wifi_settings.h>

class WiFiService : public StatefulService<WiFiSettings> {
  private:
    static void getNetworks(socket_message_NetworkListData& proto);
    static void getNetworkStatus(socket_message_NetworkStatusData& proto);
    void onStationModeDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
    void onStationModeStop(WiFiEvent_t event, WiFiEventInfo_t info);
    static void onStationModeGotIP(WiFiEvent_t event, WiFiEventInfo_t info);

    FSPersistence<WiFiSettings, socket_message_WifiSettingsData> _persistence;

    void reconfigureWiFiConnection();
    void manageSTA();
    void connectToWiFi();
    void configureNetwork(wifi_settings_t& network);

    unsigned long _lastConnectionAttempt;
    bool _stopping;

    constexpr static uint16_t reconnectDelay {10000};

  public:
    WiFiService();
    ~WiFiService();

    void begin();
    void loop();

    void setupMDNS(const char* hostname);

    const char* getHostname() { return state().hostname.c_str(); }

    static esp_err_t handleScan(HttpRequest& request);
    static esp_err_t getNetworks(HttpRequest& request);
    static esp_err_t getNetworkStatus(HttpRequest& request);

    StatefulHttpEndpoint<WiFiSettings, socket_message_WifiSettingsData> endpoint;
};
