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
#include <timing.h>
#include <settings/servo_settings.h>

#define EVENT_SERVO_CONFIGURATION_SETTINGS "servoPWM"
#define EVENT_SERVO_STATE "servoState"

class ServoController : public StatefulService<ServoSettings> {
  public:
    ServoController(PsychicHttpServer *server, FS *fs, Peripherals *peripherals)
        : _server(server),
          _peripherals(peripherals),
          endpoint(ServoSettings::read, ServoSettings::update, this),
          _persistence(ServoSettings::read, ServoSettings::update, this, &ESPFS, SERVO_SETTINGS_FILE) {}

    void begin() {
        socket.onEvent(EVENT_SERVO_CONFIGURATION_SETTINGS,
                       [&](JsonObject &root, int originId) { servoEvent(root, originId); });
        socket.onEvent(EVENT_SERVO_STATE, [&](JsonObject &root, int originId) { stateUpdate(root, originId); });
        _persistence.readFromFS();
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
    FSPersistence<ServoSettings> _persistence;

    bool is_active {true};
    constexpr static int ServoInterval = 2;

    float angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    float target_angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
};

#endif