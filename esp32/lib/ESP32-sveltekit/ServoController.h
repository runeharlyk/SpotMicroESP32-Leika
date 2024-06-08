#include <EventEndpoint.h>
#include <FSPersistence.h>
#include <HttpEndpoint.h>
#include <SecurityManager.h>
#include <StatefulService.h>

#include <Adafruit_PWMServoDriver.h>

#define SERVO_OSCILLATOR_FREQUENCY 27000000
#define SERVO_FREQ 50
#define SERVO_CONFIG_FILE "/config/servoConfig.json"
#define SERVO_CONFIGURATION_SETTINGS_PATH "/api/servo/configuration"

struct servo_t
{
    String name;
    int8_t channel;
	bool inverted;
	int16_t angle;
	int16_t center_angle;
};

class ServoConfiguration {
   public:
    int32_t servo_oscillator_frequency {SERVO_OSCILLATOR_FREQUENCY};
    int32_t servo_pwm_frequency {SERVO_FREQ};
    std::vector<servo_t> servos_config;
    bool is_active {false};

    static void read(ServoConfiguration &settings, JsonObject &root) {
        root["is_active"] = settings.is_active;
        root["servo_pwm_frequency"] = settings.servo_pwm_frequency;
        root["servo_oscillator_frequency"] = settings.servo_oscillator_frequency;

        JsonArray servos = root["servos"].to<JsonArray>();

        for (auto &servo : settings.servos_config)
        {
            JsonObject servo_config = servos.add<JsonObject>();

            servo_config["name"] = servo.name;
            servo_config["channel"] = servo.channel;
            servo_config["inverted"] = servo.inverted;
            servo_config["angle"] = servo.angle;
            servo_config["center_angle"] = servo.center_angle;
        }
    }

    static StateUpdateResult update(JsonObject &root, ServoConfiguration &settings) {
        settings.is_active = root["is_active"];
        settings.servo_pwm_frequency = root["servo_pwm_frequency"];
        settings.servo_oscillator_frequency = root["servo_oscillator_frequency"];
        settings.servos_config.clear();

        JsonArray servos = root["servos"];
        if (root["servos"].is<JsonArray>())
        {
            int i = 0;
            for (auto servo : servos)
            {
                JsonObject servo_config = servo.as<JsonObject>();
                servo_t new_servo;

                new_servo.name = servo_config["name"].as<String>();
                new_servo.channel = servo_config["channel"];
                new_servo.inverted = servo_config["inverted"];
                new_servo.angle = servo_config["angle"];
                new_servo.center_angle = servo_config["center_angle"];

                settings.servos_config.push_back(new_servo);
                i++;
            }
        }
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
        _httpEndpoint.begin();
        _fsPersistence.readFromFS();
        setOscillatorFrequency(_state.servo_oscillator_frequency);
        setPWMFreq(_state.servo_pwm_frequency);
        ESP_LOGI("ServoController", "Configured with oscillator frequency %d and PWM frequency %d", _state.servo_oscillator_frequency, _state.servo_pwm_frequency);
    }

    void deactivate() {
        _state.is_active = false;
        sleep();
    }

    void activate() {
        _state.is_active = true;
        sleep();
    }

   private:
    PsychicHttpServer *_server;
    SecurityManager *_securityManager;
    HttpEndpoint<ServoConfiguration> _httpEndpoint;
    // EventEndpoint<ServoConfiguration> _eventEndpoint;
    FSPersistence<ServoConfiguration> _fsPersistence;
};
