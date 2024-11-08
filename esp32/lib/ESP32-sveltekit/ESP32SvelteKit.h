#ifndef ESP32SvelteKit_h
#define ESP32SvelteKit_h

/**
 *   ESP32 SvelteKit
 *
 *   A simple, secure and extensible framework for IoT projects for ESP32 platforms
 *   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
 *   https://github.com/theelims/ESP32-sveltekit
 *
 *   Copyright (C) 2018 - 2023 rjwats
 *   Copyright (C) 2023 theelims
 *   Copyright (C) 2024 runeharlyk
 *
 *   All Rights Reserved. This software may be modified and distributed under
 *   the terms of the LGPL v3 license. See the LICENSE file for details.
 **/

#include <Arduino.h>

#include <analytics_service.h>
#include <BatteryService.h>
#include <filesystem.h>
#include <DownloadFirmwareService.h>
#include <Peripherals.h>
#include <ServoController.h>
#include <ESPmDNS.h>
#include <LEDService.h>
#include <event_socket.h>
#include <FeaturesService.h>
#include <MotionService.h>
#include <ntp_service.h>
#include <camera_service.h>
#include <CameraSettingsService.h>
#include <PsychicHttp.h>
#include <task_manager.h>
#include <UploadFirmwareService.h>
#include <WiFi.h>
#include <wifi_service.h>
#include <ap_service.h>
#include <Wire.h>

#ifdef EMBED_WWW
#include <WWWData.h>
#endif

#ifndef CORS_ORIGIN
#define CORS_ORIGIN "*"
#endif

#ifndef APP_VERSION
#define APP_VERSION "v1"
#endif

#ifndef APP_NAME
#define APP_NAME "SpotMicro"
#endif

#ifndef APPLICATION_CORE
#define APPLICATION_CORE -1
#endif

class ESP32SvelteKit {
  public:
    ESP32SvelteKit(PsychicHttpServer *server, unsigned int numberEndpoints = 115);

    void begin();

    FS *getFS() { return &ESPFS; }

    PsychicHttpServer *getServer() { return _server; }

    EventSocket *getSocket() { return &_socket; }

#if FT_ENABLED(USE_BATTERY)
    BatteryService *getBatteryService() { return &_batteryService; }
#endif

    FeaturesService *getFeatureService() { return &_featureService; }

#if FT_ENABLED(USE_MOTION)
    MotionService *getMotionService() { return &_motionService; }
#endif

#if FT_ENABLED(USE_CAMERA)
    Camera::CameraService *getCameraService() { return &_cameraService; }
    Camera::CameraSettingsService *getCameraSettingsService() { return &_cameraSettingsService; }
#endif

    Peripherals *getPeripherals() { return &_peripherals; }

#if FT_ENABLED(USE_SERVO)
    ServoController *getServoController() { return &_servoController; }
#endif

    void setMDNSAppName(String name) { _appName = name; }

    void recoveryMode() { _apService.recoveryMode(); }

    void loop();

  private:
    PsychicHttpServer *_server;
    unsigned int _numberEndpoints;
    FeaturesService _featureService;
    WiFiService _wifiService;
    APService _apService;
    EventSocket _socket;
#if FT_ENABLED(USE_NTP)
    NTPService _ntpService;
#endif
#if FT_ENABLED(USE_UPLOAD_FIRMWARE)
    UploadFirmwareService _uploadFirmwareService;
#endif
#if FT_ENABLED(USE_DOWNLOAD_FIRMWARE)
    DownloadFirmwareService _downloadFirmwareService;
#endif
#if FT_ENABLED(USE_BATTERY)
    BatteryService _batteryService;
#endif
#if FT_ENABLED(USE_ANALYTICS)
    AnalyticsService _analyticsService;
#endif
#if FT_ENABLED(USE_MOTION)
    MotionService _motionService;
#endif
#if FT_ENABLED(USE_CAMERA)
    Camera::CameraService _cameraService;
    Camera::CameraSettingsService _cameraSettingsService;
#endif
    Peripherals _peripherals;
    ServoController _servoController;
#if FT_ENABLED(USE_WS2812)
    LEDService _ledService;
#endif

    String _appName = APP_NAME;

  protected:
    static void _loopImpl(void *_this) { static_cast<ESP32SvelteKit *>(_this)->loop(); }
    void setupServer();
    void setupMDNS();
    void startServices();
};

#endif
