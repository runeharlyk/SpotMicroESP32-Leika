#ifndef Spot_h
#define Spot_h

#include <Arduino.h>
#include <PsychicHttp.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Wire.h>

#include <filesystem.h>
#include <peripherals/peripherals.h>
#include <peripherals/servo_controller.h>
#include <peripherals/led_service.h>
#include <event_socket.h>
#include <features.h>
#include <motion.h>
#include <wifi_service.h>
#include <ap_service.h>

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
    Spot();

    void initialize();

    // sense
    void readSensors() {
        _peripherals.readIMU();
        _peripherals.readMag();
    }

    // plan
    void planMotion() { updatedMotion = _motionService.updateMotion(); }

    // act
    void updateActuators() {
        if (updatedMotion) _servoController.setAngles(_motionService.getAngles());
        updatedMotion = false;

        _servoController.updateServoState();
#if FT_ENABLED(USE_WS2812)
        _ledService.loop();
#endif
    }

    // communicate
    void emitTelemetry() {
        if (updatedMotion) EXECUTE_EVERY_N_MS(100, { _motionService.emitAngles(); });
        EXECUTE_EVERY_N_MS(250, { _peripherals.emitIMU(); });
        // _peripherals.emitSonar();
    }

  private:
    PsychicHttpServer _server;
    WiFiService _wifiService;
    APService _apService;
    EventSocket _socket;
#if FT_ENABLED(USE_MOTION)
    MotionService _motionService;
#endif
    Peripherals _peripherals;
    ServoController _servoController;
#if FT_ENABLED(USE_WS2812)
    LEDService _ledService;
#endif

    bool updatedMotion = false;

    const char *_appName = APP_NAME;
    const u_int16_t _numberEndpoints = 120;
    const u_int32_t _maxFileUpload = 2300000; // 2.3 MB
    const uint16_t _port = 80;

  protected:
    void loop();
    static void _loopImpl(void *_this) { static_cast<Spot *>(_this)->loop(); }
    void setupServer();
    void startServices();
};

#endif
