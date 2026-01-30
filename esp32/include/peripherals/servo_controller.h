#ifndef ServoController_h
#define ServoController_h

#include <peripherals/drivers/pca9685.h>
#include <template/stateful_persistence_pb.h>
#include <template/stateful_proto_endpoint.h>
#include <template/stateful_service.h>
#include <utils/math_utils.h>
#include <platform_shared/api.pb.h>

#ifndef FACTORY_SERVO_PWM_FREQUENCY
#define FACTORY_SERVO_PWM_FREQUENCY 50
#endif

#ifndef FACTORY_SERVO_OSCILLATOR_FREQUENCY
#define FACTORY_SERVO_OSCILLATOR_FREQUENCY 27000000
#endif

enum class SERVO_CONTROL_STATE { DEACTIVATED, PWM, ANGLE };

using ServoSettings = api_ServoSettings;

inline ServoSettings ServoSettings_defaults() {
    ServoSettings settings = {};
    settings.servos_count = 12;
    const api_Servo defaults[12] = {
        {306, -1, 0, 2.2f, "Servo1"},   {306, 1, -45, 2.1055555f, "Servo2"},
        {306, 1, 90, 1.96923f, "Servo3"}, {306, -1, 0, 2.2f, "Servo4"},
        {306, -1, 45, 2.1055555f, "Servo5"}, {306, -1, -90, 1.96923f, "Servo6"},
        {306, 1, 0, 2.2f, "Servo7"},   {306, 1, -45, 2.1055555f, "Servo8"},
        {306, 1, 90, 1.96923f, "Servo9"}, {306, 1, 0, 2.2f, "Servo10"},
        {306, -1, 45, 2.1055555f, "Servo11"}, {306, -1, -90, 1.96923f, "Servo12"}
    };
    for (int i = 0; i < 12; i++) {
        settings.servos[i] = defaults[i];
    }
    return settings;
}

inline void ServoSettings_read(const ServoSettings &settings, ServoSettings &proto) {
    proto = settings;
}

inline StateUpdateResult ServoSettings_update(const ServoSettings &proto, ServoSettings &settings) {
    settings = proto;
    return StateUpdateResult::CHANGED;
}

class ServoController : public StatefulService<ServoSettings> {
  public:
    ServoController()
        : protoEndpoint(ServoSettings_read, ServoSettings_update, this,
                        API_REQUEST_EXTRACTOR(servo_settings, ServoSettings),
                        API_RESPONSE_ASSIGNER(servo_settings, ServoSettings)),
          _persistence(ServoSettings_read, ServoSettings_update, this,
                       SERVO_SETTINGS_FILE, api_ServoSettings_fields, api_ServoSettings_size,
                       ServoSettings_defaults()) {}

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
            float angle = servo.direction * angles[i] + servo.center_angle;
            uint16_t pwm = angle * servo.conversion + servo.center_pwm;
            pwms[i] = pwm = std::clamp<uint16_t>(pwm, 125, 600);
        }
        _pca.setMultiplePWM(pwms, 12);
    }

    void update() {
        if (control_state == SERVO_CONTROL_STATE::ANGLE) calculatePWM();
    }

    StatefulProtoEndpoint<ServoSettings, ServoSettings> protoEndpoint;

  private:
    void initializePCA() {
        _pca.begin();
        _pca.setOscillatorFrequency(FACTORY_SERVO_OSCILLATOR_FREQUENCY);
        _pca.setPWMFreq(FACTORY_SERVO_PWM_FREQUENCY);
        _pca.sleep();
    }
    FSPersistencePB<ServoSettings> _persistence;

    PCA9685Driver _pca;

    SERVO_CONTROL_STATE control_state = SERVO_CONTROL_STATE::DEACTIVATED;

    bool is_active {false};
    float angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    float target_angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
};

#endif
