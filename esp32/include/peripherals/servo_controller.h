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
        : protoHandler(readWithModel(), ServoSettings_update, this, api_ServoSettings_fields),
          _persistence(readWithModel(), ServoSettings_update, this, SERVO_SETTINGS_FILE, api_ServoSettings_fields,
                       api_ServoSettings_size, ServoSettings_defaults()) {}

    /**
     * The variant's joint model, set before the tasks start; null while no variant is chosen, which keeps the servos
     * asleep.
     */
    void useJointModel(const JointModel *model) { _model = model; }

    void begin() {
        _persistence.readFromFS();
        _pca.begin(FACTORY_SERVO_OSCILLATOR_FREQUENCY, FACTORY_SERVO_PWM_FREQUENCY);
    }

    bool detected() const { return _pca.isInitialized(); }

    void activate() {
        if (is_active || !_model) return;
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

    // The longest tick the speed limit spans: after a stall the joints still move no more than two ticks' worth.
    static constexpr float MAX_TICK_S = 0.02f;

    // Each joint moves toward its target no faster than the servos can, and its PWM goes to its channel; channels no
    // joint uses are written off.
    bool calculatePWM(float dt) {
        if (!_model) return false;
        const float maxStep = SERVO_MAX_SPEED_DEG_S * std::clamp(dt, 0.0f, MAX_TICK_S);
        uint32_t written = 0;
        read([&](const ServoSettings &settings) {
            std::fill(std::begin(_channelPwm), std::end(_channelPwm), 0);
            for (size_t i = 0; i < SERVO_COUNT; i++) {
                angles[i] = slewToward(angles[i], target_angles[i], maxStep);
                _outputPwm[i] = servoPwm(*_model, i, settings.servos[i].center_pwm, angles[i]);
                const uint32_t channel = jointChannel(settings, i);
                _channelPwm[channel] = _outputPwm[i];
                written = std::max(written, channel + 1);
            }
        });
        return _pca.setMultiplePWM(_channelPwm, written) == 0;
    }

    ServoWrite update(float dt) {
        if (control_state != SERVO_CONTROL_STATE::ANGLE || !_model) return {};
        return {true, calculatePWM(dt)};
    }

    const float *outputAngles() const { return angles; }
    const uint16_t *outputPwm() const { return _outputPwm; }

    StatefulProtoHandler<ServoSettings, ServoSettings> protoHandler;

  private:
    std::function<void(const ServoSettings &, ServoSettings &)> readWithModel() {
        return [this](const ServoSettings &settings, ServoSettings &proto) { ServoSettings_read(settings, proto, _model); };
    }

    const JointModel *_model = nullptr;
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
