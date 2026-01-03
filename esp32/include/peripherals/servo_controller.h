#ifndef ServoController_h
#define ServoController_h

#include <Adafruit_PWMServoDriver.h>
#include <communication/websocket_adapter.h>
#include <template/stateful_persistence.h>
#include <template/stateful_service.h>
#include <template/stateful_endpoint.h>
#include <utils/math_utils.h>
#include <settings/servo_settings.h>

/*
 * Servo Settings
 */
#ifndef FACTORY_SERVO_PWM_FREQUENCY
#define FACTORY_SERVO_PWM_FREQUENCY 50
#endif

#ifndef FACTORY_SERVO_OSCILLATOR_FREQUENCY
#define FACTORY_SERVO_OSCILLATOR_FREQUENCY 27000000
#endif

enum class SERVO_CONTROL_STATE { DEACTIVATED, PWM, ANGLE };

class ServoController : public StatefulService<ServoSettings> {
  public:
    ServoController()
        : endpoint(ServoSettings::read, ServoSettings::update, this),
          _persistence(ServoSettings::read, ServoSettings::update, this, SERVO_SETTINGS_FILE) {}

    void begin() {
        _persistence.readFromFS();

        initializePCA();
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
    }

    void deactivate() {
        if (!is_active) return;
        is_active = false;
        control_state = SERVO_CONTROL_STATE::DEACTIVATED;
        _pca.sleep();
    }

    void setServoPWM(int32_t servo_id, uint32_t pwm) {
        control_state = SERVO_CONTROL_STATE::PWM;
        if (servo_id < 0) {
            uint16_t pwms[12];
            std::fill_n(pwms, 12, static_cast<uint16_t>(pwm));
            _pca.setMultiplePWM(pwms, 12);
        } else {
            _pca.setPWM(servo_id, 0, pwm);
        }
        ESP_LOGI("SERVO_CONTROLLER", "Setting servo %d to %d", servo_id, pwm);
    }

    void updateActiveState() { is_active ? activate() : deactivate(); }

    void setMode(SERVO_CONTROL_STATE newMode) { control_state = newMode; }

    void setAngles(float new_angles[12]) {
        for (int i = 0; i < 12; i++) {
            target_angles[i] = new_angles[i];
        }
    }

    void calculatePWM() {
        uint16_t pwms[12];
        for (int i = 0; i < 12; i++) {
            angles[i] = lerp(angles[i], target_angles[i], 0.1);
            auto &servo = state().servos[i];
            float angle = servo.direction * angles[i] + servo.centerAngle;
            uint16_t pwm = angle * servo.conversion + servo.centerPwm;
            pwms[i] = pwm = std::clamp<uint16_t>(pwm, 125, 600);
        }
        _pca.setMultiplePWM(pwms, 12);
    }

    void update() {
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
};

#endif