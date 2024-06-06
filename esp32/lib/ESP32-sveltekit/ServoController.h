#include <EventEndpoint.h>
#include <FSPersistence.h>
#include <HttpEndpoint.h>
#include <SecurityManager.h>
#include <StatefulService.h>

#include <Adafruit_PWMServoDriver.h>

#define SERVO_OSCILLATOR_FREQUENCY 27000000
#define SERVO_FREQ 50
#define SERVO_CONFIG_FILE "/config/deviceConfig.json"
#define SERVO_CONFIGURATION_SETTINGS_PATH "/api/servo/configuration"


class ServoConfiguration {
   public:
    int32_t servo_oscillator_frequency {SERVO_OSCILLATOR_FREQUENCY};
    int32_t servo_pwm_frequency {SERVO_FREQ};
    static void read(ServoConfiguration &settings, JsonObject &root) {}

    static StateUpdateResult update(JsonObject &root, ServoConfiguration &settings) {
        return StateUpdateResult::CHANGED;
    };
};

class ServoController : public Adafruit_PWMServoDriver, public StatefulService<ServoConfiguration> {
   public:
    ServoController(PsychicHttpServer *server, FS *fs, SecurityManager *securityManager, EventSocket *socket)
        : Adafruit_PWMServoDriver(), _server(server),
          _securityManager(securityManager),
          _httpEndpoint(ServoConfiguration::read, ServoConfiguration::update,
                        this, server, SERVO_CONFIGURATION_SETTINGS_PATH,
                        securityManager, AuthenticationPredicates::IS_ADMIN),
        //   _eventEndpoint(ServoConfiguration::read, ServoConfiguration::update,
        //                  this, socket, EVENT_CONFIGURATION_SETTINGS),
          _fsPersistence(ServoConfiguration::read, ServoConfiguration::update, this, fs, SERVO_CONFIG_FILE) {
    }

    void configure() {
        setOscillatorFrequency(_state.servo_oscillator_frequency);
        setPWMFreq(_state.servo_pwm_frequency);
        ESP_LOGI("ServoController", "Configured with oscillator frequency %d and PWM frequency %d", _state.servo_oscillator_frequency, _state.servo_pwm_frequency);
    }

    void deactivate() {
        isActive = false;
        sleep();
    }

    void activate() {
        isActive = true;
        sleep();
    }

    bool isActive{false};

   private:
    PsychicHttpServer *_server;
    SecurityManager *_securityManager;
    HttpEndpoint<ServoConfiguration> _httpEndpoint;
    // EventEndpoint<ServoConfiguration> _eventEndpoint;
    FSPersistence<ServoConfiguration> _fsPersistence;
};
