#ifndef ServoController_h
#define ServoController_h

#include <peripherals/drivers/pca9685.h>
#include <peripherals/servo_output.h>
#include <settings/servo_settings.h>
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

    /** Calibration: one PCA9685 channel, or every channel a joint uses for -1, to a raw PWM inside the servos' range. */
    void setServoPWM(int32_t channel, uint32_t requested) {
        if (channel < -1 || channel >= static_cast<int32_t>(PCA9685_CHANNELS)) {
            ESP_LOGW("SERVO_CONTROLLER", "No channel %d", channel);
            return;
        }
        control_state = SERVO_CONTROL_STATE::PWM;
        const uint16_t pwm = boundedPwm(static_cast<float>(requested));
        if (channel >= 0) {
            _pca.setPWM(channel, 0, pwm);
        } else {
            read([&](const ServoSettings &settings) {
                for (size_t i = 0; i < SERVO_COUNT; i++) _pca.setPWM(jointChannel(settings, i), 0, pwm);
            });
        }
        ESP_LOGI("SERVO_CONTROLLER", "Setting channel %d to %d", channel, pwm);
    }

    void setMode(SERVO_CONTROL_STATE newMode) { control_state = newMode; }

    // A non-finite target is dropped: smoothing towards it would leave the joint NaN for good.
    void setAngles(float new_angles[12]) {
        for (int i = 0; i < 12; i++) {
            if (isFinite(new_angles[i])) target_angles[i] = new_angles[i];
        }
    }

    // Each joint's PWM goes to its channel; channels no joint uses are written off.
    bool calculatePWM() {
        uint32_t written = 0;
        read([&](const ServoSettings &settings) {
            std::fill(std::begin(_channelPwm), std::end(_channelPwm), 0);
            for (size_t i = 0; i < SERVO_COUNT; i++) {
                angles[i] = lerp(angles[i], target_angles[i], 0.1);
                _outputPwm[i] = servoPwm(VARIANT_JOINT_MODEL, i, settings.servos[i].center_pwm, angles[i]);
                const uint32_t channel = jointChannel(settings, i);
                _channelPwm[channel] = _outputPwm[i];
                written = std::max(written, channel + 1);
            }
        });
        return _pca.setMultiplePWM(_channelPwm, written) == 0;
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
    uint16_t _channelPwm[PCA9685_CHANNELS] = {};
};

#endif
