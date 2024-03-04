#include <globals.h>
#include <deviceconfig.h>

#if USE_WIFI && USE_WEBSERVER
    extern DRAM_ATTR CWebServer g_WebServer;
#endif

#if USE_WIFI

bool ConnectToWiFi(uint cRetries) {
    static bool bPreviousConnection = false;

    if (WiFi.isConnected()) return true;

    log_i("Connection to wifi");
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);

    for (uint iPass = 0; iPass < cRetries; iPass++) {
        log_i("Pass %u of %u: Connecting to Wifi SSID: \"%s\" - ESP32 Free Memory: %u, PSRAM:%u, PSRAM Free: %u\n",
                iPass + 1, cRetries, SSID, ESP.getFreeHeap(), ESP.getPsramSize(), ESP.getFreePsram());

        WiFi.begin(SSID, PASS);
        delay(4000 + iPass * 1000);

        if (WiFi.isConnected()) {
            log_i("Connected to AP with BSSID: %s\n", WiFi.BSSIDstr().c_str());
            break;
        }
    }

    // Additional Services onwwards reliant on network so close if not up.
    if (false == WiFi.isConnected()) {
        log_i("Giving up on WiFi\n");
        return false;
    }

    log_i("Received IP: %s", WiFi.localIP().toString().c_str());

    // If we were connected before, network-dependent services will have been started already
    if (bPreviousConnection)
        return true;

    #if USE_OTA
        //debugI("Publishing OTA...");
        SetupOTA(String(cszHostname));
    #endif

    #if USE_NTP
        //debugI("Setting Clock...");
        //NTPTimeClient::UpdateClockFromWeb(&g_Udp);
    #endif

    #if USE_WEBSERVER
        g_WebServer.begin();
    #endif
    
    #if USE_MDNS
    if(MDNS.begin(HOSTNAME)){
        MDNS.addService("http", "tcp", HTTP_PORT);
    }
    #endif

    bPreviousConnection = true;
    return true;
}

void IRAM_ATTR NetworkHandlingLoopEntry(void *) {
    unsigned long lastWifiCheck = 0;
    unsigned long checkWiFiEveryMs = 1000;

    TickType_t notifyWait = 0;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, notifyWait);

        if ( millis() - lastWifiCheck > checkWiFiEveryMs) {
            if (WiFi.isConnected() == false && ConnectToWiFi(5) == false) {
                log_e("Cannot Connect to Wifi!");
                #if WAIT_FOR_WIFI
                    log_e("Rebooting in 5 seconds due to no Wifi available.");
                    delay(5000);
                    throw new std::runtime_error("Rebooting due to no Wifi available.");
                #endif
            }
        }
        #if USE_WEBSERVER
        g_WebServer.loop();
        #endif

        notifyWait = pdMS_TO_TICKS(1000);
    }
}

#endif // ENABLE_WIFI