#include <Adafruit_PWMServoDriver.h>
#include <EventEndpoint.h>
#include <FSPersistence.h>
#include <HttpEndpoint.h>
#include <SecurityManager.h>
#include <StatefulService.h>

#define SERVO_CONFIG_FILE "/config/servoConfig.json"
#define SERVO_CONFIGURATION_SETTINGS_PATH "/api/servo/configuration"
#define EVENT_CONFIGURATION_SETTINGS "servoConfiguration"

#ifndef FACTORY_SERVO_NUM
#define FACTORY_SERVO_NUM 12
#endif

#ifndef FACTORY_SERVO_PWM_FREQUENCY
#define FACTORY_SERVO_PWM_FREQUENCY 50
#endif

#ifndef FACTORY_SERVO_OSCILLATOR_FREQUENCY
#define FACTORY_SERVO_OSCILLATOR_FREQUENCY 27000000
#endif

#ifndef FACTORY_SERVO_CENTER_ANGLE
#define FACTORY_SERVO_CENTER_ANGLE 90
#endif

#define SERVO_STATE_SPEED_MS 20 
#define SERVO_MIN 150  // This is the 'minimum' pulse length count (out of 4096)
#define SERVO_MAX 650  // This is the 'maximum' pulse length count (out of 4096)

enum SERVO_STATE { SERVO_STATE_ACTIVE, SERVO_STATE_SWEEPING_FORWARD, SERVO_STATE_SWEEPING_BACKWARD };

struct servo_t {
    String name;
    int8_t channel;
    bool inverted;
    int16_t angle;
    int16_t center_angle;
    SERVO_STATE state;
};

class ServoConfiguration {
   public:
    int32_t servo_oscillator_frequency{FACTORY_SERVO_OSCILLATOR_FREQUENCY};
    int32_t servo_pwm_frequency{FACTORY_SERVO_PWM_FREQUENCY};
    std::vector<servo_t> servos_config;
    bool is_active{true};
    const int8_t servo_invert[12] = {-1, 1, 1, -1, -1, -1, 1, 1, 1, 1, -1, -1};

    static void read(ServoConfiguration &settings, JsonObject &root) {
        root["is_active"] = settings.is_active;
        root["servo_pwm_frequency"] = settings.servo_pwm_frequency;
        root["servo_oscillator_frequency"] =
            settings.servo_oscillator_frequency;

        JsonArray servos = root["servos"].to<JsonArray>();

        for (auto &servo : settings.servos_config) {
            JsonObject servo_config = servos.add<JsonObject>();

            servo_config["name"] = servo.name;
            servo_config["channel"] = servo.channel;
            servo_config["inverted"] = servo.inverted;
            servo_config["angle"] = servo.angle;
            servo_config["center_angle"] = servo.center_angle;
            servo_config["state"] = servo.state;
        }
    }

    static StateUpdateResult update(JsonObject &root,
                                    ServoConfiguration &settings) {
        settings.is_active = root["is_active"] | settings.is_active;
        settings.servo_pwm_frequency =
            root["servo_pwm_frequency"] | settings.servo_pwm_frequency;
        settings.servo_oscillator_frequency =
            root["servo_oscillator_frequency"] |
            settings.servo_oscillator_frequency;

        JsonArray servos = root["servos"];

        if (!root["servos"].is<JsonArray>() && settings.servos_config.empty()) {
            ESP_LOGI("ControllerSettings", "No servos found, adding default servos");
            for (int8_t i = 0; i < FACTORY_SERVO_NUM; i++) {
                ESP_LOGI("ControllerSettings", "Adding servo %d", i);
                settings.servos_config.push_back(
                    servo_t{.name = "Servo " + String(i),
                            .channel = i,
                            .inverted = 1,
                            .angle = 0,
                            .center_angle = FACTORY_SERVO_CENTER_ANGLE
                            // ,
                            // .state = SERVO_STATE_ACTIVE
                            });
            }
            return StateUpdateResult::CHANGED;
        }

        for (auto new_servo_json : servos) {
            JsonObject servo_config = new_servo_json.as<JsonObject>();
            int8_t channel = servo_config["channel"] | -1;
            servo_t *servo =
                get_servo_by_channel(settings.servos_config, channel);

            if (servo != nullptr) {
                servo->name = servo_config["name"].as<String>() || servo->name;
                if (servo_config["inverted"])
                    servo->inverted = servo_config["inverted"];
                if (servo_config["angle"].is<int16_t>()) {
                    servo->angle = servo_config["angle"].as<int16_t>();
                    servo->state = SERVO_STATE_ACTIVE;
                }
                if (servo_config["center_angle"].is<int16_t>())
                    servo->center_angle =
                        servo_config["center_angle"].as<int16_t>();
                if (servo_config["sweep"])
                    servo->state = SERVO_STATE_SWEEPING_FORWARD;
                continue;
            }

            servo_t new_servo;
            new_servo.name = servo_config["name"].as<String>();
            new_servo.channel = channel;
            new_servo.inverted = servo_config["inverted"];
            new_servo.angle = servo_config["angle"];
            new_servo.center_angle = servo_config["center_angle"];
            // new_servo.state = servo_config["state"];

            settings.servos_config.push_back(new_servo);
        }

        return StateUpdateResult::CHANGED;
    };
    static servo_t *get_servo_by_channel(std::vector<servo_t> &servos_config,
                                         int8_t channel_id) {
        for (auto &servo : servos_config) {
            if (servo.channel == channel_id) {
                return &servo;
            }
        }
        return nullptr;
    }
};

class ServoController : public Adafruit_PWMServoDriver,
                        public StatefulService<ServoConfiguration> {
   public:
    ServoController(PsychicHttpServer *server, FS *fs,
                    SecurityManager *securityManager, EventSocket *socket)
        : Adafruit_PWMServoDriver(),
          _server(server),
          _securityManager(securityManager),
          _eventEndpoint(ServoConfiguration::read, ServoConfiguration::update,
                         this, socket, EVENT_CONFIGURATION_SETTINGS),
          _fsPersistence(ServoConfiguration::read, ServoConfiguration::update,
                         this, fs, SERVO_CONFIG_FILE) {
        addUpdateHandler([&](const String &originId) { updateServos(); },
                         false);
    }

    void configure() {
        _eventEndpoint.begin();
        _fsPersistence.readFromFS();
        setOscillatorFrequency(_state.servo_oscillator_frequency);
        setPWMFreq(_state.servo_pwm_frequency);
        deactivate();
        ESP_LOGI("ServoController",
                 "Configured with oscillator frequency %d and PWM frequency %d",
                 _state.servo_oscillator_frequency, _state.servo_pwm_frequency);
    }

    void deactivate() {
        if (!is_active) return;
        _state.is_active = false;
        is_active = false;
        sleep();
    }

    void activate() {
        if (is_active) return;
        _state.is_active = true;
        is_active = true;
        wakeup();
    }

    void updateActiveState() { _state.is_active ? activate() : deactivate(); }

    void updateServos() {
        updateActiveState();

        if (!is_active) return;

        for (auto &servo : _state.servos_config) {
            setAngle(&servo);
        }
    }

    void setAngle(servo_t* servo) {
        int8_t channel = servo->channel;
        bool invert = servo->inverted;
        int16_t angle = invert ? 180 - servo->angle : servo->angle;
        ESP_LOGV("ServoController", "Setting servo %d to angle %d", channel, angle);
        setPWM(channel, 0, angleToPwm(angle));
    }

    uint16_t angleToPwm(int angle) {
        return map(angle, 0, 180, SERVO_MIN, SERVO_MAX);
    }

    void updateServoState() {
        for (auto &servo : _state.servos_config) {
            if (servo.state == SERVO_STATE::SERVO_STATE_ACTIVE) {
                continue;
            } else if (servo.state == SERVO_STATE::SERVO_STATE_SWEEPING_FORWARD) {
                servo.angle += 1;
                if (servo.angle >= 180) {
                    servo.state = SERVO_STATE::SERVO_STATE_SWEEPING_BACKWARD;
                }
            } else if (servo.state == SERVO_STATE::SERVO_STATE_SWEEPING_BACKWARD) {
                servo.angle -= 1;
                if (servo.angle <= 0) {
                    servo.state = SERVO_STATE::SERVO_STATE_ACTIVE;
                }
            }
            setAngle(&servo);
        }
    }

    void loop() {
        if (int currentMillis = millis(); !_lastUpdate || (currentMillis - _lastUpdate) >= ServoInterval) {
            _lastUpdate = currentMillis;
            updateServoState();
        }
    }

   private:
    PsychicHttpServer *_server;
    SecurityManager *_securityManager;
    EventEndpoint<ServoConfiguration> _eventEndpoint;
    FSPersistence<ServoConfiguration> _fsPersistence;

    bool is_active{true};
    unsigned long _lastUpdate;
    constexpr static int ServoInterval = 100;
};
