#ifndef ServoController_h
#define ServoController_h

#include <Adafruit_PWMServoDriver.h>
#include <event_socket.h>
#include <template/stateful_persistence.h>
#include <template/stateful_service.h>
#include <template/stateful_endpoint.h>
#include <utils/math_utils.h>
#include <settings/servo_settings.h>

/*
 * Servo Settings
 */
#ifndef FACTORY_SERVO_PWM_FREQUENCY
#define FACTORY_SERVO_PWM_FREQUENCY 60
#endif

#ifndef FACTORY_SERVO_OSCILLATOR_FREQUENCY
#define FACTORY_SERVO_OSCILLATOR_FREQUENCY 27000000
#endif

#ifndef FACTORY_SERVO_SMOOTHING_FACTOR
#define FACTORY_SERVO_SMOOTHING_FACTOR 0.05
#endif

#define EVENT_SERVO_CONFIGURATION_SETTINGS "servoPWM"
#define EVENT_SERVO_STATE "servoState"

enum class SERVO_CONTROL_STATE { DEACTIVATED, PWM, ANGLE };

class ServoController : public StatefulService<ServoSettings> {
  public:
    ServoController()
        : endpoint(ServoSettings::read, ServoSettings::update, this),
          _persistence(ServoSettings::read, ServoSettings::update, this, SERVO_SETTINGS_FILE) {}

    void begin() {
        socket.onEvent(EVENT_SERVO_CONFIGURATION_SETTINGS,
                       [&](JsonObject &root, int originId) { servoEvent(root, originId); });
        socket.onEvent(EVENT_SERVO_STATE, [&](JsonObject &root, int originId) { stateUpdate(root, originId); });
        _persistence.readFromFS();

        initializePCA();
        socket.onEvent(EVENT_SERVO_STATE, [&](JsonObject &root, int originId) {
            is_active = root["active"] | false;
            is_active ? activate() : deactivate();
        });
    }

    void pcaWrite(int index, int value) {
        if (value < 0 || value > 4096) {
            ESP_LOGE("Peripherals", "Invalid PWM value %d for %d :: Valid range 0-4096", value, index);
            return;
        }
        _pca.setPWM(index, 0, value);
    }

    void activate() {
        if (is_active) return;
        control_state = SERVO_CONTROL_STATE::ANGLE;
        is_active = true;
        _pca.wakeup();
        for (int i = 0; i < 12; i++) {
            angles[i] = target_angles[i] + 1.0;
        }
    }

    void deactivate() {
        if (!is_active) return;
        is_active = false;
        control_state = SERVO_CONTROL_STATE::DEACTIVATED;
        _pca.sleep();
    }

    void stateUpdate(JsonObject &root, int originId) {
        bool active = root["active"].as<bool>();
        ESP_LOGI("SERVOCONTROLLER", "Setting state %d", active);
        active ? activate() : deactivate();
    }

    void servoEvent(JsonObject &root, int originId) {
        control_state = SERVO_CONTROL_STATE::PWM;
        uint8_t servo_id = root["servo_id"];
        int pwm = root["pwm"].as<int>();
        pcaWrite(servo_id, pwm);
        ESP_LOGI("SERVO_CONTROLLER", "Setting servo %d to %d", servo_id, pwm);
    }

    void syncAngles(const String &originId) {
        char output[100];
        snprintf(output, sizeof(output), "[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]", angles[0],
                 angles[1], angles[2], angles[3], angles[4], angles[5], angles[6], angles[7], angles[8], angles[9],
                 angles[10], angles[11]);
        socket.emit("angles", output, String(originId).c_str());
    }

    void updateActiveState() { is_active ? activate() : deactivate(); }

    void setAngles(float new_angles[12]) {
        control_state = SERVO_CONTROL_STATE::ANGLE;
        for (int i = 0; i < 12; i++) {
            if (abs(target_angles[i] - new_angles[i]) > 0.01) {
                target_angles[i] = new_angles[i];
            }
        }
    }

    void calculatePWM() {
        bool any_changes = false;
        unsigned long current_time = millis();

        for (int i = 0; i < 12; i++) {
            if (abs(angles[i] - target_angles[i]) <= 0.01) continue;

            angles[i] = lerp(angles[i], target_angles[i], FACTORY_SERVO_SMOOTHING_FACTOR);
            auto &servo = state().servos[i];
            float angle = servo.direction * angles[i] + servo.centerAngle;
            uint16_t pwm = angle * servo.conversion + servo.centerPwm;

            if (pwm < 125 || pwm > 600) {
                ESP_LOGE("ServoController", "Servo %d, Invalid PWM value %d", i, pwm);
                continue;
            }

            _current_pwm[i] = pwm;
            any_changes = true;
        }

        if (any_changes || (current_time - _last_resend_time >= RESEND_INTERVAL)) {
            _pca.setMultiplePWM(_current_pwm, 12);
            _last_resend_time = current_time;
        }
    }

    void updateServoState() {
        if (control_state == SERVO_CONTROL_STATE::ANGLE) calculatePWM();
    }

    StatefulHttpEndpoint<ServoSettings> endpoint;

  private:
    void initializePCA() {
        _pca.begin();
        _pca.setPWMFreq(FACTORY_SERVO_PWM_FREQUENCY);
        _pca.setOscillatorFrequency(FACTORY_SERVO_OSCILLATOR_FREQUENCY);
        _pca.sleep();
    }
    FSPersistence<ServoSettings> _persistence;

    Adafruit_PWMServoDriver _pca;

    SERVO_CONTROL_STATE control_state = SERVO_CONTROL_STATE::DEACTIVATED;

    bool is_active {false};
    float angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    float target_angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    uint16_t _current_pwm[12];
    unsigned long _last_resend_time = 0;
    const unsigned long RESEND_INTERVAL = 1000;
};

#endif