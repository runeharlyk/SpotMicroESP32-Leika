#pragma once

#include <SPIFFS.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <NewPing.h>

// Disable brownout problems
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"     

/*
 * Macros
 */
#define NAME_OF(x)          #x

/*
 * Feature flags
 */

#include <featureflags.h>

#if USE_WIFI
    #include <WiFi.h>
#endif

#if USE_WIFI && USE_WEBSERVER
    #if USE_WEBSERVER_SSL
        #define ASYNC_TCP_SSL_ENABLED 1
        #include <ESPAsyncTCP.h>
    #endif
    #include <ESPAsyncWebServer.h>
#endif

#if USE_OAT
    #include <ArduinoOTA.h>
#endif

#if USE_DNS_SERVER
    #include <DNSServer.h>
#endif

#if USE_MDNS
    #include <ESPmDNS.h>
#endif

#define STACK_SIZE (ESP_TASK_MAIN_STACK) // Stack size for each new thread

/*
 * Thread priority
 */
#define NET_PRIORITY            tskIDLE_PRIORITY+5
#define MOVEMENT_PRIORITY       tskIDLE_PRIORITY+3
#define JSONWRITER_PRIORITY     tskIDLE_PRIORITY+2



/*
 * Thread core
 */
#define NET_CORE                1
#define MOVEMENT_CORE           0
#define JSONWRITER_CORE         0


/*
 * Main include
 */
#include <taskmanager.h>
#include <movement.h>
#include <secrets.h>
#include <servo.h>

#if USE_CAMERA

    #include <camera.h>
#endif

#if USE_WIFI && USE_WEBSERVER
    #include <webserver.h>
#endif

#if USE_WIFI
    #include <network.h>
#endif
