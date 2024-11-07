#ifndef ServoController_h
#define ServoController_h

#include <Adafruit_PWMServoDriver.h>
#include <EventEndpoint.h>
#include <event_socket.h>
#include <FSPersistence.h>
#include <HttpEndpoint.h>
#include <StatefulService.h>
#include <stateful_service_endpoint.h>
#include <MathUtils.h>
#include <Timing.h>

#define EVENT_SERVO_CONFIGURATION_SETTINGS "servoPWM"
#define EVENT_SERVO_STATE "servoState"

typedef struct {
    float centerPwm;
    float direction;
    float centerAngle;
    float conversion;
    const char *name;
    // bool enable;
} servo_settings_t;

class ServoSettings {
  public:
    servo_settings_t servos[12] = {
        {306, -1, 0, 2.2, "Servo1"}, {306, 1, -45, 2.1055555, "Servo2"},  {306, 1, 90, 1.96923, "Servo3"},
        {306, -1, 0, 2.2, "Servo4"}, {306, -1, 45, 2.1055555, "Servo5"},  {306, -1, -90, 1.96923, "Servo6"},
        {306, 1, 0, 2.2, "Servo7"},  {306, 1, -45, 2.1055555, "Servo8"},  {306, 1, 90, 1.96923, "Servo9"},
        {306, 1, 0, 2.2, "Servo10"}, {306, -1, 45, 2.1055555, "Servo11"}, {306, -1, -90, 1.96923, "Servo12"}};

    static void read(ServoSettings &settings, JsonObject &root) {
        JsonArray servos = root["servos"].to<JsonArray>();

        for (auto &servo : settings.servos) {
            JsonObject newServo = servos.add<JsonObject>();

            newServo["center_pwm"] = servo.centerPwm;
            newServo["direction"] = servo.direction;
            newServo["center_angle"] = servo.centerAngle;
            newServo["conversion"] = servo.conversion;
        }
    }

    static StateUpdateResult update(JsonObject &root, ServoSettings &settings) {
        if (root["servos"].is<JsonArray>()) {
            JsonArray servosJson = root["servos"];
            int i = 0;
            for (auto servo : servosJson) {
                JsonObject servoObject = servo.as<JsonObject>();

                uint8_t servoId = i; // servoObject["id"].as<uint8_t>();

                settings.servos[servoId].centerPwm = servoObject["center_pwm"].as<float>();
                settings.servos[servoId].centerAngle = servoObject["center_angle"].as<float>();
                settings.servos[servoId].direction = servoObject["direction"].as<float>();
                settings.servos[servoId].conversion = servoObject["conversion"].as<float>();

                i++;
            }
        }
        ESP_LOGI("ServoController", "Updating servo data");

        return StateUpdateResult::CHANGED;
    };
};

class ServoController : public StatefulService<ServoSettings> {
  public:
    ServoController(PsychicHttpServer *server, FS *fs, Peripherals *peripherals)
        : _server(server),
          _peripherals(peripherals),
          endpoint(ServoSettings::read, ServoSettings::update, this),
          _fsPersistence(ServoSettings::read, ServoSettings::update, this, &ESPFS, SERVO_SETTINGS_FILE) {}

    void begin() {
        socket.onEvent(EVENT_SERVO_CONFIGURATION_SETTINGS,
                       [&](JsonObject &root, int originId) { servoEvent(root, originId); });
        socket.onEvent(EVENT_SERVO_STATE, [&](JsonObject &root, int originId) { stateUpdate(root, originId); });
        _fsPersistence.readFromFS();
    }

    void stateUpdate(JsonObject &root, int originId) {
        bool active = root["active"].as<bool>();
        ESP_LOGI("SERVOCONTROLLER", "Setting state %d", active);
        active ? activate() : deactivate();
    }

    void servoEvent(JsonObject &root, int originId) {
        uint8_t servo_id = root["servo_id"];
        uint16_t pwm = root["pwm"];
        ESP_LOGI("SERVO_CONTROLLER", "Setting servo %d to %d", servo_id, pwm);
    }

    void syncAngles(const String &originId) {
        char output[100];
        snprintf(output, sizeof(output), "[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]", angles[0],
                 angles[1], angles[2], angles[3], angles[4], angles[5], angles[6], angles[7], angles[8], angles[9],
                 angles[10], angles[11]);
        socket.emit("angles", output, String(originId).c_str());
    }

    void deactivate() { _peripherals->pcaDeactivate(); }

    void activate() { _peripherals->pcaActivate(); }

    void updateActiveState() { is_active ? activate() : deactivate(); }

    void setAngles(float new_angles[12]) {
        for (int i = 0; i < 12; i++) {
            target_angles[i] = new_angles[i];
        }
    }

    void updateServoState() {
        for (int i = 0; i < 12; i++) {
            angles[i] = lerp(angles[i], target_angles[i], 0.2);
            auto &servo = _state.servos[i];
            float angle = servo.direction * angles[i] + servo.centerAngle;
            uint16_t pwm = angle * servo.conversion + servo.centerPwm;
            if (pwm < 125 || pwm > 600) {
                ESP_LOGE("ServoController", "Servo %d, Invalid PWM value %d", i, pwm);
                continue;
            }
            _peripherals->pcaWrite(i, pwm);
        }
    }

    void loop() {
        EXECUTE_EVERY_N_MS(ServoInterval, { updateServoState(); });
    }

    StatefulHttpEndpoint<ServoSettings> endpoint;

  private:
    PsychicHttpServer *_server;
    Peripherals *_peripherals;
    FSPersistence<ServoSettings> _fsPersistence;

    bool is_active {true};
    constexpr static int ServoInterval = 2;

    float angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    float target_angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
};

#endif