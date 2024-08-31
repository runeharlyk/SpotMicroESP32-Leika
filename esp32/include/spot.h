#ifndef Spot_h
#define Spot_h

#include <Arduino.h>

#include <features.h>
#include <webserver.h>
#include <task_manager.h>

#include <event_socket.h>

#include <services/peripherals/led_service.h>
#include <services/peripherals/peripherals.h>
#include <services/peripherals/servo.h>

#include <motion.h>

namespace spot {

class Spot {
  private:
    PsychicHttpServer *_server;
    FS *_fs {&ESPFS};
    WebServer _webserver;
    WiFiService _wifiService;
    APService _apService;
    SystemService _systemService;
    NTPService _ntpService;
    camera::CameraService _cameraService;
    LEDService _ledService;
    MotionService _motionService;
    EventSocket _socket;
    ServoController _servoController;
    Peripherals _peripherals;

    JsonDocument doc;
    char message[2000];
    const char *name = "spot-micro";

  protected:
    static void _loopImpl(void *_this) { static_cast<Spot *>(_this)->_loop(); }
    void _loop();
    void startServices();

  public:
    Spot(PsychicHttpServer *server);
    ~Spot();

    void begin();
    void beginAsync();
    void loop();
};

} // namespace spot

#endif
