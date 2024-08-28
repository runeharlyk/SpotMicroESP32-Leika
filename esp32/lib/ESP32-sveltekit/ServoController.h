#ifndef ServoController_h
#define ServoController_h

#include <Adafruit_PWMServoDriver.h>
#include <EventEndpoint.h>
#include <FSPersistence.h>
#include <HttpEndpoint.h>
#include <SecurityManager.h>
#include <StatefulService.h>
#include <MathUtils.h>
#include <Timing.h>

#define EVENT_SERVO_ANGLE "servoAngle"
#define EVENT_SERVO_PWM "servoPWM"
#define EVENT_SERVO_STATE "servoState"

enum class SERVO_STATE { DEACTIVATED, ACTIVE, SINGLE_PWM, ALL_PWM };

class ServoController {
  public:
    ServoController(PsychicHttpServer *server, FS *fs, SecurityManager *securityManager, Peripherals *peripherals,
                    EventSocket *socket)
        : _server(server), _securityManager(securityManager), _peripherals(peripherals), _socket(socket) {}

    void begin() {
        _socket->onEvent(EVENT_SERVO_ANGLE, [&](JsonObject &root, int originId) { angleEvent(root, originId); });
        _socket->onEvent(EVENT_SERVO_PWM, [&](JsonObject &root, int originId) { pwmEvent(root, originId); });
        _socket->onEvent(EVENT_SERVO_STATE, [&](JsonObject &root, int originId) { handleMode(root, originId); });
    }

    void handleMode(JsonObject &root, int originId) {
        state = (SERVO_STATE)root["data"].as<int>();
        state == SERVO_STATE::DEACTIVATED ? deactivate() : activate();
        ESP_LOGI("SERVO_CONTROLLER", "Updates state");
    }

    void angleEvent(JsonObject &root, int originId) {
        uint8_t servo_id = root["servo_id"];
        uint16_t angle = root["angle"];
        angles[servo_id] = angle;
        ESP_LOGI("SERVO_CONTROLLER", "Setting servo %d to %d degree", servo_id, angle);
    }

    void pwmEvent(JsonObject &root, int originId) {
        uint16_t pwm = root["pwm"];
        if (state == SERVO_STATE::SINGLE_PWM) {
            uint8_t servo_id = root["servo_id"];
            pwm_signal[servo_id] = pwm;
            ESP_LOGI("SERVO_CONTROLLER", "Setting servo %d to %d", servo_id, pwm);
        } else if (state == SERVO_STATE::ALL_PWM) {
            for (int i = 0; i < 12; i++) {
                pwm_signal[i] = pwm;
            }
            ESP_LOGI("SERVO_CONTROLLER", "Setting all servos to %d", pwm);
        }
    }

    void syncAngles(const String &originId) {
        char output[100];
        snprintf(output, sizeof(output), "[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]", angles[0],
                 angles[1], angles[2], angles[3], angles[4], angles[5], angles[6], angles[7], angles[8], angles[9],
                 angles[10], angles[11]);
        _socket->emit("angles", output, String(originId).c_str());
    }

    void deactivate() { _peripherals->pcaDeactivate(); }

    void activate() { _peripherals->pcaActivate(); }

    void setAngles(float new_angles[12]) {
        for (int i = 0; i < 12; i++) {
            target_angles[i] = new_angles[i];
        }
    }

    void updateServoState() {
        for (int i = 0; i < 12; i++) {
            if (state == SERVO_STATE::SINGLE_PWM || state == SERVO_STATE::ALL_PWM) {
                _peripherals->pcaWrite(i, pwm_signal[i]);
            } else {
                angles[i] = lerp(angles[i], target_angles[i], 0.2);
                float angle = dir[i] * angles[i] + center_angle_deg[i];
                uint16_t pwm = angle * servo_conversion[i] + center[i];
                if (pwm < 125 || pwm > 600) {
                    ESP_LOGE("ServoController", "Servo %d, Invalid PWM value %d", i, pwm);
                    continue;
                }
                _peripherals->pcaWrite(i, pwm);
            }
        }
    }

    void loop() {
        EXECUTE_EVERY_N_MS(ServoInterval, { updateServoState(); });
    }

  private:
    PsychicHttpServer *_server;
    SecurityManager *_securityManager;
    Peripherals *_peripherals;
    EventSocket *_socket;

    SERVO_STATE state {SERVO_STATE::DEACTIVATED};
    constexpr static int ServoInterval = 2;

    uint16_t center[12] = {306, 306, 306, 306, 306, 306, 306, 306, 306, 306, 306, 306};
    uint16_t pwm_signal[12] = {306, 306, 306, 306, 306, 306, 306, 306, 306, 306, 306, 306};
    int8_t dir[12] = {-1, 1, 1, -1, -1, -1, 1, 1, 1, 1, -1, -1};
    float center_angle_deg[12] = {0, -45, 90, 0, 45, -90, 0, -45, 90, 0, 45, -90};

    float angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    float target_angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    const float servo_conversion[12] {2.2, 2.1055555, 1.96923, 2.2, 2.1055555, 1.96923,
                                      2.2, 2.1055555, 1.96923, 2.2, 2.1055555, 1.96923};
};

#endif