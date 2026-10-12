#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>
#include <peripherals/imu/imu_driver.h>
#include <peripherals/imu/madgwick.h>

struct ImuConfig {
    Mat3 mounting = IDENTITY3;      // IMU chip frame to body frame
    Mat3 magAlignment = IDENTITY3;  // magnetometer chip frame to IMU chip frame
    Vec3 magOffset {};              // microtesla, hard iron
    Mat3 magSoftIron = IDENTITY3;
    float fusionGain = 0.1f;
};

/**
 * Turns one IMU chip, and a separate compass when there is one, into body-frame samples with a fused orientation.
 * Runs on the sensor task only; callers serialise access.
 */
class Imu {
  public:
    static constexpr int64_t FAST_START_US = 2000000;  // high gain after start, so the first estimate is quick
    static constexpr float FAST_START_GAIN = 2.5f;
    static constexpr float STILL_GYRO_STD = 0.02f;      // rad/s; well above the chips' noise, below any handling
    static constexpr float STILL_TILT_RAD = 0.01f;      // gravity turning this far between the halves is a rotation
    static constexpr int BIAS_SAMPLES = 200;
    static constexpr float MAX_DT_S = 0.05f;           // a longer gap is a stall, not motion to integrate
    static constexpr int MAG_STALE_PERIODS = 3;         // a compass silent this long is treated as gone

    /** The IMU chips to try, in order, and a separate compass if one may be fitted. */
    Imu(std::vector<ImuDriver *> candidates, MagDriver *mag) : _candidates(std::move(candidates)), _mag(mag) {}

    Imu(ImuDriver *driver, MagDriver *mag) : Imu(std::vector<ImuDriver *> {driver}, mag) {}

    /**
     * Starts the first candidate that answers; without `useMag` no compass reading is used, the chip's own or a
     * separate one. Returns whether an IMU answered.
     */
    bool begin(int64_t nowUs, bool useMag = true) {
        _driver = nullptr;
        for (ImuDriver *candidate : _candidates) {
            if (candidate && candidate->begin()) {
                _driver = candidate;
                break;
            }
        }
        _ready = _driver != nullptr;
        _useMag = useMag;
        _hasSeparateMag = _ready && useMag && _mag && _mag->begin();
        _magValid = false;
        _lastUs = 0;
        _dueUs = 0;
        _startUs = nowUs;
        _filter.reset();
        return _ready;
    }

    /** Leaves the chips alone and reports no IMU, for one that is wired but not mounted. */
    void stop() {
        _driver = nullptr;
        _ready = false;
        _hasSeparateMag = false;
        _magValid = false;
    }

    /** Whether a compass feeds the samples: the IMU chip's own or a separate one. */
    bool hasMag() const { return magRateHz() > 0; }

    /** Whether the IMU chip has a compass of its own, used or not. */
    bool chipHasMag() const { return _ready && _driver->magRateHz() > 0; }

    void configure(const ImuConfig &config) { _config = config; }

    bool ready() const { return _ready; }
    const char *driverName() const { return _ready ? _driver->name() : "none"; }
    uint32_t rateHz() const { return _ready ? _driver->rateHz() : 0; }
    uint32_t magRateHz() const {
        if (!_ready || !_useMag) return 0;
        return _hasSeparateMag ? _mag->rateHz() : _driver->magRateHz();
    }

    /**
     * Averages the gyro over BIAS_SAMPLES reads while the robot is still. A robot that moves keeps the previous
     * bias and gets false: one whose gyro spreads, or whose gravity turns between the first and the second half, which
     * a slow steady tilt does without any spread. A steady turn about the vertical stays indistinguishable from bias. `readClockAndWait` waits for the next read and returns the time in microseconds. With
     * `meanAccel`, a still robot also gets the mean accelerometer, in the body frame: its measured up.
     */
    bool estimateGyroBias(const std::function<int64_t()> &readClockAndWait, Vec3 *meanAccel = nullptr) {
        if (!_ready) return false;
        // The orientation stays; only the second spent here must not count as one integration step.
        _lastUs = 0;
        _dueUs = 0;
        double sum[3] = {0, 0, 0}, sumSquares[3] = {0, 0, 0}, accelSum[2][3] = {{0, 0, 0}, {0, 0, 0}};
        for (int i = 0; i < BIAS_SAMPLES; i++) {
            readClockAndWait();
            RawImu raw;
            if (!_driver->read(raw)) return false;
            for (int axis = 0; axis < 3; axis++) {
                sum[axis] += raw.gyro[axis];
                sumSquares[axis] += raw.gyro[axis] * raw.gyro[axis];
                accelSum[2 * i / BIAS_SAMPLES][axis] += raw.accel[axis];
            }
        }
        Vec3 bias, accel, firstHalf, secondHalf;
        for (int axis = 0; axis < 3; axis++) {
            const double mean = sum[axis] / BIAS_SAMPLES;
            const double variance = sumSquares[axis] / BIAS_SAMPLES - mean * mean;
            if (std::sqrt(std::fmax(variance, 0.0)) > STILL_GYRO_STD) return false;
            bias[axis] = static_cast<float>(mean);
            firstHalf[axis] = static_cast<float>(accelSum[0][axis]);
            secondHalf[axis] = static_cast<float>(accelSum[1][axis]);
            accel[axis] = static_cast<float>((accelSum[0][axis] + accelSum[1][axis]) / BIAS_SAMPLES);
        }
        if (angleBetween(firstHalf, secondHalf) > STILL_TILT_RAD) return false;
        _gyroBias = bias;
        if (meanAccel) *meanAccel = mul(_config.mounting, accel);
        return true;
    }

    /** Reads the chip when its next sample is due, so a chip slower than the sensor task is not read twice. */
    bool update(int64_t nowUs, ImuSample &out) {
        if (!_ready) return false;
        // A quarter period early still counts as due, so the pacing timer's jitter does not skip a sample.
        const int64_t period = 1000000 / _driver->rateHz();
        if (_dueUs && nowUs < _dueUs - period / 4) return false;
        _dueUs = _dueUs + period > nowUs ? _dueUs + period : nowUs + period;
        RawImu raw;
        if (!_driver->read(raw)) return false;
        ImuSample sample;
        sample.t_us = nowUs;
        sample.accel = mul(_config.mounting, raw.accel);
        sample.gyro = mul(_config.mounting, sub(raw.gyro, _gyroBias));
        sample.temperature = raw.temperature;
        sample.valid = ImuValid::ACCEL | ImuValid::GYRO;
        takeMag(nowUs, raw);
        const bool magFresh = _magValid && nowUs - _magUs <= MAG_STALE_PERIODS * (1000000 / magRateHz());
        if (magFresh) {
            sample.mag = _magBody;
            sample.mag_t_us = _magUs;
            sample.valid |= ImuValid::MAG;
        }

        const float dt = _lastUs ? std::min((nowUs - _lastUs) * 1e-6f, MAX_DT_S) : 0;
        _lastUs = nowUs;
        if (raw.hasOrientation) {
            sample.quat = quatMul(raw.quat, quatConj(quatFromMatrix(_config.mounting)));
        } else {
            _filter.setGain(nowUs - _startUs < FAST_START_US ? FAST_START_GAIN : _config.fusionGain);
            if (magFresh) {
                _filter.update(sample.gyro, sample.accel, sample.mag, dt);
            } else {
                _filter.updateImu(sample.gyro, sample.accel, dt);
                sample.valid |= ImuValid::YAW_DRIFTS;
            }
            sample.quat = _filter.quaternion();
        }
        sample.valid |= ImuValid::ORIENTATION;
        sample.gravity = gravityInBody(sample.quat);
        sample.rpy = rpyFromQuat(sample.quat);
        out = sample;
        return true;
    }

  private:
    static float angleBetween(const Vec3 &a, const Vec3 &b) {
        const float cross[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
        const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        return std::atan2(std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]), dot);
    }

    std::vector<ImuDriver *> _candidates;
    ImuDriver *_driver = nullptr;
    MagDriver *_mag;
    bool _useMag = true;
    ImuConfig _config;
    Madgwick _filter;
    bool _ready = false;
    bool _hasSeparateMag = false;
    Vec3 _gyroBias {};  // chip frame, so a later mounting change leaves it right
    int64_t _startUs = 0;
    int64_t _lastUs = 0;
    int64_t _dueUs = 0;
    bool _magValid = false;
    Vec3 _magBody {};
    int64_t _magUs = 0;
    int64_t _magDueUs = 0;

    // The compass is slower than the IMU: its last reading is reused until the next one is due.
    void takeMag(int64_t nowUs, const RawImu &raw) {
        if (!_useMag) return;
        Vec3 field;
        bool fresh = false;
        if (raw.hasMag) {
            field = raw.mag;
            fresh = true;
        } else if (_hasSeparateMag && nowUs >= _magDueUs) {
            // Stepping from the last due time keeps the compass's rate; after a stall it starts afresh, no burst.
            const int64_t period = 1000000 / _mag->rateHz();
            _magDueUs = _magDueUs + period > nowUs ? _magDueUs + period : nowUs + period;
            fresh = _mag->read(field);
        }
        if (!fresh) return;
        const Vec3 calibrated = mul(_config.magSoftIron, sub(field, _config.magOffset));
        _magBody = mul(_config.mounting, mul(_config.magAlignment, calibrated));
        _magUs = nowUs;
        _magValid = true;
    }
};
