#ifndef Spot_h
#define Spot_h

#include <Arduino.h>

#include <BatteryService.h>
#include <filesystem.h>
#include <firmware_download_service.h>
#include <firmware_upload_service.h>
#include <Peripherals.h>
#include <ServoController.h>
#include <ESPmDNS.h>
#include <LEDService.h>
#include <event_socket.h>
#include <features.h>
#include <MotionService.h>
#include <ntp_service.h>
#include <camera_service.h>
#include <CameraSettingsService.h>
#include <PsychicHttp.h>
#include <task_manager.h>
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

class Spot {
  public:
    Spot(PsychicHttpServer *server);

    void initialize();

    // sense
    void readSensors() { _peripherals.readIMU(); }

    // plan
    void planMotion() { updatedMotion = _motionService.updateMotion(); }

    // act
    void updateActuators() {
        if (updatedMotion) _servoController.setAngles(_motionService.getAngles());

        _servoController.updateServoState();
#if FT_ENABLED(USE_WS2812)
        _ledService.loop();
#endif
    }

    // communicate
    void emitTelemetry() {
        if (updatedMotion) EXECUTE_EVERY_N_MS(100, { _motionService.syncAngles(); });
        // _peripherals.loop();
        EXECUTE_EVERY_N_MS(1000, { _peripherals.emitIMU(); });
        // _peripherals.emitSonar();
        // _peripherals.emitBattery();
    }

  private:
    PsychicHttpServer *_server;
    WiFiService _wifiService;
    APService _apService;
    EventSocket _socket;
#if FT_ENABLED(USE_NTP)
    NTPService _ntpService;
#endif
#if FT_ENABLED(USE_UPLOAD_FIRMWARE)
    FirmwareUploadService _uploadFirmwareService;
#endif
#if FT_ENABLED(USE_DOWNLOAD_FIRMWARE)
    DownloadFirmwareService _downloadFirmwareService;
#endif
#if FT_ENABLED(USE_BATTERY)
    BatteryService _batteryService;
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

    bool updatedMotion = false;

    String _appName = APP_NAME;
    const u_int16_t _numberEndpoints = 115;
    const u_int32_t _maxFileUpload = 2300000; // 2.3 MB
    const uint16_t _port = 80;

  protected:
    void loop();
    static void _loopImpl(void *_this) { static_cast<Spot *>(_this)->loop(); }
    void setupServer();
    void setupMDNS();
    void startServices();
};

#endif
