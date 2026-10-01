#pragma once

#include <cmath>
#include <functional>
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
    static constexpr int BIAS_SAMPLES = 200;

    Imu(ImuDriver *driver, MagDriver *mag) : _driver(driver), _mag(mag) {}

    bool begin(int64_t nowUs) {
        _ready = _driver && _driver->begin();
        _hasSeparateMag = _ready && _mag && _mag->begin();
        _startUs = nowUs;
        _filter.reset();
        return _ready;
    }

    void configure(const ImuConfig &config) { _config = config; }

    bool ready() const { return _ready; }
    const char *driverName() const { return _ready ? _driver->name() : "none"; }
    uint32_t rateHz() const { return _ready ? _driver->rateHz() : 0; }
    uint32_t magRateHz() const {
        if (!_ready) return 0;
        return _hasSeparateMag ? _mag->rateHz() : _driver->magRateHz();
    }

    /**
     * Averages the gyro over BIAS_SAMPLES reads while the robot is still. A robot that moves keeps the previous
     * bias and gets false. `readClockAndWait` waits for the next read and returns the time in microseconds.
     */
    bool estimateGyroBias(const std::function<int64_t()> &readClockAndWait) {
        if (!_ready) return false;
        double sum[3] = {0, 0, 0}, sumSquares[3] = {0, 0, 0};
        for (int i = 0; i < BIAS_SAMPLES; i++) {
            readClockAndWait();
            RawImu raw;
            if (!_driver->read(raw)) return false;
            const Vec3 gyro = mul(_config.mounting, raw.gyro);
            for (int axis = 0; axis < 3; axis++) {
                sum[axis] += gyro[axis];
                sumSquares[axis] += gyro[axis] * gyro[axis];
            }
        }
        Vec3 bias;
        for (int axis = 0; axis < 3; axis++) {
            const double mean = sum[axis] / BIAS_SAMPLES;
            const double variance = sumSquares[axis] / BIAS_SAMPLES - mean * mean;
            if (std::sqrt(std::fmax(variance, 0.0)) > STILL_GYRO_STD) return false;
            bias[axis] = static_cast<float>(mean);
        }
        _gyroBias = bias;
        _startUs = readClockAndWait();
        _filter.reset();
        return true;
    }

    bool update(int64_t nowUs, ImuSample &out) {
        RawImu raw;
        if (!_ready || !_driver->read(raw)) return false;
        ImuSample sample;
        sample.t_us = nowUs;
        sample.accel = mul(_config.mounting, raw.accel);
        sample.gyro = sub(mul(_config.mounting, raw.gyro), _gyroBias);
        sample.temperature = raw.temperature;
        sample.valid = ImuValid::ACCEL | ImuValid::GYRO;
        takeMag(nowUs, raw);
        if (_magValid) {
            sample.mag = _magBody;
            sample.mag_t_us = _magUs;
            sample.valid |= ImuValid::MAG;
        }

        const float dt = _lastUs ? (nowUs - _lastUs) * 1e-6f : 0;
        _lastUs = nowUs;
        if (raw.hasOrientation) {
            sample.quat = quatMul(raw.quat, quatConj(quatFromMatrix(_config.mounting)));
        } else {
            _filter.setGain(nowUs - _startUs < FAST_START_US ? FAST_START_GAIN : _config.fusionGain);
            if (_magValid) {
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
    ImuDriver *_driver;
    MagDriver *_mag;
    ImuConfig _config;
    Madgwick _filter;
    bool _ready = false;
    bool _hasSeparateMag = false;
    Vec3 _gyroBias {};
    int64_t _startUs = 0;
    int64_t _lastUs = 0;
    bool _magValid = false;
    Vec3 _magBody {};
    int64_t _magUs = 0;
    int64_t _magDueUs = 0;

    // The compass is slower than the IMU: its last reading is reused until the next one is due.
    void takeMag(int64_t nowUs, const RawImu &raw) {
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
