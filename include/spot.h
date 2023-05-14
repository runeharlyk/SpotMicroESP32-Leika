#ifndef SPOT_h
#define SPOT_h

#include <SPI.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <NewPing.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <Adafruit_GFX.h>
#include <MPU6050_light.h>
#include <Adafruit_SSD1306.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_PWMServoDriver.h>

// Server functions
#include <AsyncJpegStreamHandler.h>
#include <WebsocketHandler.h>

// Disable brownout problems
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"           

// Config
#include <config.h>
#include <camera_pins.h>

#ifdef __cplusplus
  extern "C" {
    #endif
    uint8_t temprature_sens_read();
    #ifdef __cplusplus
  }
#endif
uint8_t temprature_sens_read();

class Spot {
    public:
        Spot();

        esp_err_t boot();
        void handle();

        esp_err_t initialize_wifi();

        uint8_t cpu_temperature();
    
    private:
        esp_err_t _initialize_camera();
        esp_err_t _initialize_captive_portal();
        esp_err_t _initialize_arduino_oat();
        esp_err_t _initialize_wifi_connection();
        esp_err_t _initialize_server();
        esp_err_t _initialize_display();
        esp_err_t _initialize_mpu();
        esp_err_t _initialize_pwm_controller();
        esp_err_t _initialize_button();

        DNSServer _dnsServer;
        AsyncEventSource _events;
        AsyncWebSocket _ws;
        AsyncWebServer _server;

        Adafruit_SSD1306 _display;
        Adafruit_PWMServoDriver _pwm;
        MPU6050 _mpu;
        NewPing _leftUss;
        NewPing _rightUss;
};

void display_ip_and_ssid(Adafruit_SSD1306* display, String ip, const char* ssid);

#endif