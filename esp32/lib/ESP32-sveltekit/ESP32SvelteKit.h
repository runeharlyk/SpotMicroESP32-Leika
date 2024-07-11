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

#include <APSettingsService.h>
#include <APStatus.h>
#include <AnalyticsService.h>
#include <AuthenticationService.h>
#include <BatteryService.h>
#include <FileExplorerService.h>
#include <DownloadFirmwareService.h>
#include <Peripherals.h>
#include <ServoController.h>
#include <ESPFS.h>
#include <ESPmDNS.h>
#include <LEDService.h>
#include <EventSocket.h>
#include <FactoryResetService.h>
#include <FeaturesService.h>
#include <MotionService.h>
#include <NTPSettingsService.h>
#include <CameraService.h>
#include <CameraSettingsService.h>
#include <NTPStatus.h>
#include <PsychicHttp.h>
#include <RestartService.h>
#include <SecuritySettingsService.h>
#include <SleepService.h>
#include <SystemStatus.h>
#include <TaskManager.h>
#include <UploadFirmwareService.h>
#include <WiFi.h>
#include <WiFiScanner.h>
#include <WiFiSettingsService.h>
#include <WiFiStatus.h>
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

#ifndef ESP32SVELTEKIT_RUNNING_CORE
#define ESP32SVELTEKIT_RUNNING_CORE -1
#endif

class ESP32SvelteKit {
  public:
    ESP32SvelteKit(PsychicHttpServer *server, unsigned int numberEndpoints = 115);

    void begin();

    FS *getFS() { return &ESPFS; }

    PsychicHttpServer *getServer() { return _server; }

    SecurityManager *getSecurityManager() { return &_securitySettingsService; }

    EventSocket *getSocket() { return &_socket; }

    StatefulService<WiFiSettings> *getWiFiSettingsService() { return &_wifiSettingsService; }

    StatefulService<APSettings> *getAPSettingsService() { return &_apSettingsService; }

#if FT_ENABLED(FT_NTP)
    StatefulService<NTPSettings> *getNTPSettingsService() { return &_ntpSettingsService; }
#endif

#if FT_ENABLED(FT_SLEEP)
    SleepService *getSleepService() { return &_sleepService; }
#endif

#if FT_ENABLED(FT_BATTERY)
    BatteryService *getBatteryService() { return &_batteryService; }
#endif

    FeaturesService *getFeatureService() { return &_featureService; }

    TaskManager *getTaskManager() { return &_taskManager; }

    FileExplorer *getFileExplorer() { return &_fileExplorer; }

#if FT_ENABLED(FT_MOTION)
    MotionService *getMotionService() { return &_motionService; }
#endif

#if FT_ENABLED(FT_CAMERA)
    CameraService *getCameraService() { return &_cameraService; }
    CameraSettingsService *getCameraSettingsService() { return &_cameraSettingsService; }
#endif

    Peripherals *getPeripherals() { return &_peripherals; }

#if FT_ENABLED(FT_SERVO)
    ServoController *getServoController() { return &_servoController; }
#endif

    void factoryReset() { _factoryResetService.factoryReset(); }

    void setMDNSAppName(String name) { _appName = name; }

    void recoveryMode() { _apSettingsService.recoveryMode(); }

    void loop();

  private:
    PsychicHttpServer *_server;
    unsigned int _numberEndpoints;
    FeaturesService _featureService;
    SecuritySettingsService _securitySettingsService;
    WiFiSettingsService _wifiSettingsService;
    WiFiScanner _wifiScanner;
    WiFiStatus _wifiStatus;
    APSettingsService _apSettingsService;
    APStatus _apStatus;
    EventSocket _socket;
#if FT_ENABLED(FT_NTP)
    NTPSettingsService _ntpSettingsService;
    NTPStatus _ntpStatus;
#endif
#if FT_ENABLED(FT_UPLOAD_FIRMWARE)
    UploadFirmwareService _uploadFirmwareService;
#endif
#if FT_ENABLED(FT_DOWNLOAD_FIRMWARE)
    DownloadFirmwareService _downloadFirmwareService;
#endif
#if FT_ENABLED(FT_SECURITY)
    AuthenticationService _authenticationService;
#endif
#if FT_ENABLED(FT_SLEEP)
    SleepService _sleepService;
#endif
#if FT_ENABLED(FT_BATTERY)
    BatteryService _batteryService;
#endif
#if FT_ENABLED(FT_ANALYTICS)
    AnalyticsService _analyticsService;
#endif
    RestartService _restartService;
    FactoryResetService _factoryResetService;
    SystemStatus _systemStatus;
    TaskManager _taskManager;
    FileExplorer _fileExplorer;
#if FT_ENABLED(FT_MOTION)
    MotionService _motionService;
#endif
#if FT_ENABLED(FT_CAMERA)
    CameraService _cameraService;
    CameraSettingsService _cameraSettingsService;
#endif
    Peripherals _peripherals;
#if FT_ENABLED(FT_SERVO)
    ServoController _servoController;
#endif
#if FT_ENABLED(FT_WS2812)
    LEDService _ledService;
#endif

    String _appName = APP_NAME;

  protected:
    void setupServer();
    void setupMDNS();
    void startServices();
};

#endif
