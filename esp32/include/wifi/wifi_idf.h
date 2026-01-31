#pragma once

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <utils/ip_address.h>
#include <string>
#include <cstring>
#include <functional>
#include <vector>

typedef enum {
    WL_NO_SHIELD = 255,
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL = 1,
    WL_SCAN_COMPLETED = 2,
    WL_CONNECTED = 3,
    WL_CONNECT_FAILED = 4,
    WL_CONNECTION_LOST = 5,
    WL_DISCONNECTED = 6
} wl_status_t;

typedef enum {
    WIFI_AUTH_OPEN_IDF = 0,
    WIFI_AUTH_WEP_IDF,
    WIFI_AUTH_WPA_PSK_IDF,
    WIFI_AUTH_WPA2_PSK_IDF
} wifi_enc_type_t;

using WiFiEventCb = std::function<void(int32_t event, void* event_data)>;

struct WiFiEventHandler {
    int32_t event_id;
    WiFiEventCb callback;
};

class WiFiClass {
  public:
    WiFiClass();
    ~WiFiClass();

    bool init();

    bool mode(wifi_mode_t mode);
    wifi_mode_t getMode();

    bool begin(const char* ssid, const char* password = nullptr, int32_t channel = 0, const uint8_t* bssid = nullptr);
    bool disconnect(bool wifiOff = false);
    bool reconnect();

    bool config(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dns1 = IPAddress(),
                IPAddress dns2 = IPAddress());
    bool setHostname(const char* hostname);
    const char* getHostname();

    wl_status_t status();
    bool isConnected();

    IPAddress localIP();
    IPAddress subnetMask();
    IPAddress gatewayIP();
    IPAddress dnsIP(uint8_t dns_no = 0);

    std::string macAddress();
    std::string SSID();
    std::string BSSIDstr();
    int32_t RSSI();
    uint8_t channel();

    int16_t scanNetworks(bool async = false);
    int16_t scanComplete();
    void scanDelete();

    std::string SSID(uint8_t i);
    int32_t RSSI(uint8_t i);
    wifi_enc_type_t encryptionType(uint8_t i);
    std::string BSSIDstr(uint8_t i);
    int32_t channel(uint8_t i);
    void getNetworkInfo(uint8_t i, std::string& ssid, uint8_t& encType, int32_t& rssi, uint8_t*& bssid, int32_t& ch);

    bool softAP(const char* ssid, const char* password = nullptr, int channel = 1, bool ssid_hidden = false,
                int max_connection = 4);
    bool softAPConfig(IPAddress local_ip, IPAddress gateway, IPAddress subnet);
    bool softAPdisconnect(bool wifiOff = false);
    IPAddress softAPIP();
    std::string softAPmacAddress();
    uint8_t softAPgetStationNum();

    bool setAutoReconnect(bool autoReconnect);
    bool persistent(bool persistent);
    bool setTxPower(int8_t power);

    void onEvent(WiFiEventCb callback, int32_t event_id);

  private:
    static void eventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    void dispatchEvent(int32_t event_id, void* event_data);

    esp_netif_t* _sta_netif;
    esp_netif_t* _ap_netif;
    bool _initialized;
    bool _autoReconnect;
    bool _persistent;
    wl_status_t _status;
    wifi_mode_t _mode;

    std::string _hostname;
    wifi_ap_record_t* _scanResult;
    uint16_t _scanCount;
    int16_t _scanStatus;

    std::vector<WiFiEventHandler> _eventHandlers;

    IPAddress _sta_static_ip;
    IPAddress _sta_static_gw;
    IPAddress _sta_static_sn;
    IPAddress _sta_static_dns1;
    IPAddress _sta_static_dns2;
    bool _useStaticIp;
};

extern WiFiClass WiFi;
