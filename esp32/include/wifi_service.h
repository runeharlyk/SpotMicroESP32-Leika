#pragma once

#include <esp_http_server.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <string>

#include <filesystem.h>
#include <utils/timing.h>
#include <template/stateful_service.h>
#include <template/stateful_persistence_pb.h>
#include <template/stateful_proto_endpoint.h>
#include <settings/wifi_settings.h>

class WiFiService : public StatefulService<WiFiSettings> {
  private:
    void onStationModeDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
    void onStationModeStop(WiFiEvent_t event, WiFiEventInfo_t info);
    static void onStationModeGotIP(WiFiEvent_t event, WiFiEventInfo_t info);

    FSPersistencePB<WiFiSettings> _persistence;

    void reconfigureWiFiConnection();
    void manageSTA();
    void connectToWiFi();
    void configureNetwork(WiFiNetwork &network);

    unsigned long _lastConnectionAttempt;
    bool _stopping;

    constexpr static uint16_t reconnectDelay {10000};

  public:
    WiFiService();
    ~WiFiService();

    void begin();
    void loop();

    void setupMDNS(const char *hostname);

    const char *getHostname() { return state().hostname; }

    static esp_err_t handleScan(httpd_req_t *request);
    static esp_err_t getNetworks(httpd_req_t *request);
    static esp_err_t getNetworkStatus(httpd_req_t *request);

    StatefulProtoEndpoint<WiFiSettings, api_WifiSettings> protoEndpoint;
};
