#pragma once

#include <PsychicHttp.h>
#include <IPAddress.h>
#include <WiFi.h>
#include <ESPmDNS.h>

#include <filesystem.h>
#include <utils/timing.h>
#include <template/stateful_service.h>
#include <template/stateful_persistence.h>
#include <template/stateful_endpoint.h>
#include <settings/wifi_settings.h>

class WiFiService : public StatefulService<WiFiSettings> {
  private:
    static void getNetworks(JsonObject &root);
    static void getNetworkStatus(JsonObject &root);
    void onStationModeDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
    void onStationModeStop(WiFiEvent_t event, WiFiEventInfo_t info);
    static void onStationModeGotIP(WiFiEvent_t event, WiFiEventInfo_t info);

    FSPersistence<WiFiSettings> _persistence;

    void reconfigureWiFiConnection();
    void manageSTA();
    void connectToWiFi();
    void configureNetwork(wifi_settings_t &network);

    unsigned long _lastConnectionAttempt;
    bool _stopping;

    constexpr static uint16_t reconnectDelay {10000};

  public:
    WiFiService();
    ~WiFiService();

    void begin();
    void loop();

    void setupMDNS(const char *hostname);

    const char *getHostname() { return state().hostname.c_str(); }

    static esp_err_t handleScan(PsychicRequest *request);
    static esp_err_t getNetworks(PsychicRequest *request);
    static esp_err_t getNetworkStatus(PsychicRequest *request);

    StatefulHttpEndpoint<WiFiSettings> endpoint;
};