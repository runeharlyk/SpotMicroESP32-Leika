#pragma once

#include <wifi/wifi_idf.h>
#include <string>
#include <atomic>

#include <filesystem.h>
#include <utils/timing.h>
#include <template/stateful_service.h>
#include <template/stateful_persistence.h>
#include <template/stateful_proto_handler.h>
#include <settings/wifi_settings.h>
#include <settings/app_network_settings.h>
#include <settings/factory_network.h>
#include <secrets.h>

#define WIFI_EVENT_STA_DISCONNECTED_IDF WIFI_EVENT_STA_DISCONNECTED
#define IP_EVENT_STA_GOT_IP_IDF 1000

class WiFiService : public StatefulService<WiFiSettings> {
  public:
    WiFiService();
    ~WiFiService();

    void begin();
    void loop();

    std::string getHostname() const {
        std::string hostname;
        read([&hostname](const WiFiSettings &settings) { hostname = settings.hostname; });
        return hostname;
    }

    /** Starts a scan unless one is running. */
    static void startScan();
    /** The last scan's networks; false while a scan is still running. */
    static bool scanResults(api_WifiNetworkList &list);
    static void status(api_WifiStatus &status);

    StatefulProtoHandler<WiFiSettings, api_WifiSettings> protoHandler;

  private:
    void onStationModeDisconnected(int32_t event, void *event_data);
    void onStationModeGotIP(int32_t event, void *event_data);

    FSPersistencePB<WiFiSettings> _persistence;

    void mergeFactoryNetwork();
    void reconfigureWiFiConnection();
    void manageSTA();
    void configureNetwork(const WiFiNetwork &network, const char *hostname);

    // Set from the socket's task when settings change; the service task applies it.
    std::atomic<uint32_t> _reconfigureAt {0};
    uint32_t _nextAttemptAt {0};
    uint32_t _nextNetwork {0};
    uint32_t _connectingTo {0};

    constexpr static uint32_t reconnectDelay {10000};
    // Leaves time for the reply to the save that caused the change.
    constexpr static uint32_t reconfigureDelay {500};
};
