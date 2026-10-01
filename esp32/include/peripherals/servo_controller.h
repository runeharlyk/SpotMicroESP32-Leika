#ifndef ServoController_h
#define ServoController_h

#include <peripherals/drivers/pca9685.h>
#include <peripherals/servo_output.h>
#include <template/stateful_persistence.h>
#include <template/stateful_proto_handler.h>
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
        {306, -1, 0, 2.0f, "Servo1"}, {306, 1, -45, 2.0f, "Servo2"},  {306, 1, 90, 2.0f, "Servo3"},
        {306, -1, 0, 2.0f, "Servo4"}, {306, -1, 45, 2.0f, "Servo5"},  {306, -1, -90, 2.0f, "Servo6"},
        {306, 1, 0, 2.0f, "Servo7"},  {306, 1, -45, 2.0f, "Servo8"},  {306, 1, 90, 2.0f, "Servo9"},
        {306, 1, 0, 2.0f, "Servo10"}, {306, -1, 45, 2.0f, "Servo11"}, {306, -1, -90, 2.0f, "Servo12"}};
    for (int i = 0; i < 12; i++) {
        settings.servos[i] = defaults[i];
    }
    return settings;
}

inline void ServoSettings_read(const ServoSettings &settings, ServoSettings &proto) { proto = settings; }

inline StateUpdateResult ServoSettings_update(const ServoSettings &proto, ServoSettings &settings) {
    if (!validServoSettings(proto)) return StateUpdateResult::ERROR;
    settings = proto;
    return StateUpdateResult::CHANGED;
}

struct ServoWrite {
    bool attempted = false;
    bool ok = false;
};

class ServoController : public StatefulService<ServoSettings> {
  public:
    ServoController()
        : protoHandler(ServoSettings_read, ServoSettings_update, this, api_ServoSettings_fields),
          _persistence(ServoSettings_read, ServoSettings_update, this, SERVO_SETTINGS_FILE, api_ServoSettings_fields,
                       api_ServoSettings_size, ServoSettings_defaults()) {}

    void begin() {
        _persistence.readFromFS();
        initializePCA();
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

    /** Calibration: one servo (0-11), or all for -1, to a raw PWM kept inside the servos' range. */
    void setServoPWM(int32_t servo_id, uint32_t requested) {
        if (servo_id < -1 || servo_id >= static_cast<int32_t>(SERVO_COUNT)) {
            ESP_LOGW("SERVO_CONTROLLER", "No servo %d", servo_id);
            return;
        }
        control_state = SERVO_CONTROL_STATE::PWM;
        uint16_t pwm = boundedPwm(static_cast<float>(requested));
        if (servo_id < 0) {
            uint16_t pwms[SERVO_COUNT];
            std::fill_n(pwms, SERVO_COUNT, pwm);
            _pca.setMultiplePWM(pwms, SERVO_COUNT);
        } else {
            _pca.setPWM(servo_id, 0, pwm);
        }
        ESP_LOGI("SERVO_CONTROLLER", "Setting servo %d to %d", servo_id, pwm);
    }

    void setMode(SERVO_CONTROL_STATE newMode) { control_state = newMode; }

    // A non-finite target is dropped: smoothing towards it would leave the joint NaN for good.
    void setAngles(float new_angles[12]) {
        for (int i = 0; i < 12; i++) {
            if (isFinite(new_angles[i])) target_angles[i] = new_angles[i];
        }
    }

    bool calculatePWM() {
        // A save from the app rewrites the calibration on the socket's task: each tick uses one whole copy.
        read([&](const ServoSettings &settings) {
            for (int i = 0; i < SERVO_COUNT; i++) {
                angles[i] = lerp(angles[i], target_angles[i], 0.1);
                _outputPwm[i] = servoPwm(settings.servos[i], angles[i]);
            }
        });
        return _pca.setMultiplePWM(_outputPwm, SERVO_COUNT) == 0;
    }

    ServoWrite update() {
        if (control_state != SERVO_CONTROL_STATE::ANGLE) return {};
        return {true, calculatePWM()};
    }

    const float *outputAngles() const { return angles; }
    const uint16_t *outputPwm() const { return _outputPwm; }

    StatefulProtoHandler<ServoSettings, ServoSettings> protoHandler;

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
    uint16_t _outputPwm[SERVO_COUNT] = {};
};

#endif
