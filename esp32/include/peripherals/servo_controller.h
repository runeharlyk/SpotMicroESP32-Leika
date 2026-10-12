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
#include <esp_timer.h>
#include <atomic>

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
     * The variant's joint model, set from the control task, or before the tasks start; null while no variant is
     * chosen, which keeps the servos asleep. The socket's task reads it for the settings it reports.
     */
    void useJointModel(const JointModel *model) { _model.store(model); }

    void begin() {
        _persistence.readFromFS();
        _pca.begin(FACTORY_SERVO_OSCILLATOR_FREQUENCY, FACTORY_SERVO_PWM_FREQUENCY);
    }

    bool detected() const { return _pca.isInitialized(); }

    bool calibrated() {
        bool result = false;
        read([&](const ServoSettings &settings) { result = servosCalibrated(settings); });
        return result;
    }

    void activate() {
        if (is_active || !_model.load()) return;
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

    /**
     * Targets in servo space. A non-finite target is dropped: smoothing towards it would leave the joint NaN for good.
     * A target past the joint's travel (joint_model.h) is held at its end, from the gait, a clip or the app alike.
     */
    void setAngles(const float new_angles[12]) {
        const JointModel *model = _model.load();
        for (size_t i = 0; i < SERVO_COUNT; i++) {
            if (!isFinite(new_angles[i])) continue;
            target_angles[i] = model ? toTravel(*model, i, new_angles[i]) : new_angles[i];
        }
    }

    // The longest tick the speed limit spans: after a stall the joints still move no more than two ticks' worth.
    static constexpr float MAX_TICK_S = 0.02f;

    // Each joint moves toward its target no faster than the servos can, and its PWM goes to its channel; channels no
    // joint uses are written off.
    bool calculatePWM(float dt) {
        const JointModel *model = _model.load();
        if (!model) return false;
        const float maxStep = SERVO_MAX_SPEED_DEG_S * std::clamp(dt, 0.0f, MAX_TICK_S);
        uint32_t written = 0;
        read([&](const ServoSettings &settings) {
            std::fill(std::begin(_channelPwm), std::end(_channelPwm), 0);
            for (size_t i = 0; i < SERVO_COUNT; i++) {
                angles[i] = slewToward(angles[i], target_angles[i], maxStep);
                _outputPwm[i] = servoPwm(*model, i, settings.servos[i].center_pwm, angles[i]);
                const uint32_t channel = jointChannel(settings, i);
                _channelPwm[channel] = _outputPwm[i];
                written = std::max(written, channel + 1);
            }
        });
        return _pca.setMultiplePWM(_channelPwm, written) == 0;
    }

    /**
     * A board that stops answering costs every write the bus's timeout, longer than a tick: after a few failed writes
     * in a row the controller only tries twice a second, and writes every tick again once the board answers. A
     * skipped write is not ok.
     */
    static constexpr uint32_t FAILURES_BEFORE_BACKOFF = 3;
    static constexpr int64_t RETRY_INTERVAL_US = 500000;

    ServoWrite update(float dt) {
        if (control_state != SERVO_CONTROL_STATE::ANGLE || !_model.load()) return {};
        const int64_t now = esp_timer_get_time();
        if (_failedWrites >= FAILURES_BEFORE_BACKOFF && now - _lastAttemptUs < RETRY_INTERVAL_US) return {};
        _lastAttemptUs = now;
        const bool ok = calculatePWM(dt);
        if (ok && _failedWrites > 0) restartBoardIfReset();
        _failedWrites = ok ? 0 : _failedWrites + 1;
        return {true, ok};
    }

    const float *outputAngles() const { return angles; }
    const uint16_t *outputPwm() const { return _outputPwm; }

    StatefulProtoHandler<ServoSettings, ServoSettings> protoHandler;

  private:
    // The travel is in the IK convention, which JOINT_DIRECTION turns into servo space and back.
    float toTravel(const JointModel &model, size_t joint, float angle) {
        const float ik = angle * JOINT_DIRECTION[joint];
        const float held = clampToTravel(model, joint, ik);
        if (held != ik) noteClamp(jointTravel(model, joint), joint, ik);
        return held * JOINT_DIRECTION[joint];
    }

    static constexpr int64_t CLAMP_LOG_PERIOD_US = 2000000;

    // A clamped target means the motion asks for more than the legs can do; it says so with the furthest it asked
    // for, but not at 100 Hz.
    void noteClamp(const JointRange &range, size_t joint, float ik) {
        const float past = std::max(range.min - ik, ik - range.max);
        _clampsSinceLog++;
        if (past > _worstPast) {
            _worstPast = past;
            _worstJoint = joint;
            _worstAngle = ik;
        }
        const int64_t now = esp_timer_get_time();
        if (now - _lastClampLog < CLAMP_LOG_PERIOD_US) return;
        ESP_LOGW("SERVO_CONTROLLER", "%lu targets held at joint travel; furthest joint %u at %.1f deg (IK)",
                 static_cast<unsigned long>(_clampsSinceLog), static_cast<unsigned>(_worstJoint), _worstAngle);
        _lastClampLog = now;
        _clampsSinceLog = 0;
        _worstPast = 0;
    }

    std::function<void(const ServoSettings &, ServoSettings &)> readWithModel() {
        return [this](const ServoSettings &settings, ServoSettings &proto) {
            ServoSettings_read(settings, proto, _model.load());
        };
    }

    // A board that answers again after failed writes may have lost power: then it answers asleep, at its default
    // frequency and without auto-increment, so the write that reached it landed in one register.
    void restartBoardIfReset() {
        if (_pca.configured()) return;
        _pca.begin(FACTORY_SERVO_OSCILLATOR_FREQUENCY, FACTORY_SERVO_PWM_FREQUENCY);
        if (is_active) _pca.wakeup();
    }

    std::atomic<const JointModel *> _model {nullptr};
    FSPersistencePB<ServoSettings> _persistence;

    PCA9685Driver _pca;

    SERVO_CONTROL_STATE control_state = SERVO_CONTROL_STATE::DEACTIVATED;

    bool is_active {false};
    float angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    float target_angles[12] = {0, 90, -145, 0, 90, -145, 0, 90, -145, 0, 90, -145};
    uint16_t _outputPwm[SERVO_COUNT] = {};
    uint16_t _channelPwm[PCA9685_CHANNELS] = {};
    int64_t _lastClampLog = -CLAMP_LOG_PERIOD_US;
    uint32_t _clampsSinceLog = 0;
    float _worstPast = 0;
    size_t _worstJoint = 0;
    float _worstAngle = 0;
    uint32_t _failedWrites = 0;
    int64_t _lastAttemptUs = 0;
};

#endif
