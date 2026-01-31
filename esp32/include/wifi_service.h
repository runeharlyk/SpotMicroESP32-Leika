#pragma once

#include <esp_http_server.h>
#include <wifi/wifi_idf.h>
#include <mdns.h>
#include <string>

#include <filesystem.h>
#include <utils/timing.h>
#include <template/stateful_service.h>
#include <template/stateful_persistence_pb.h>
#include <template/stateful_proto_endpoint.h>
#include <settings/wifi_settings.h>

#define WIFI_EVENT_STA_DISCONNECTED_IDF WIFI_EVENT_STA_DISCONNECTED
#define WIFI_EVENT_STA_STOP_IDF WIFI_EVENT_STA_STOP
#define IP_EVENT_STA_GOT_IP_IDF 1000

class WiFiService : public StatefulService<WiFiSettings> {
  private:
    static void getNetworks(JsonObject &root);
    static void getNetworkStatus(JsonObject &root);
    void onStationModeDisconnected(int32_t event, void *event_data);
    void onStationModeStop(int32_t event, void *event_data);
    static void onStationModeGotIP(int32_t event, void *event_data);

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
