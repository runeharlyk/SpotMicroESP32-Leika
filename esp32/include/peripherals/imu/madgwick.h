#pragma once

#include <cmath>
#include <peripherals/imu/imu_math.h>

/**
 * Madgwick's gradient-descent orientation filter. The quaternion rotates body vectors into a world frame whose z
 * points up and whose x points to magnetic north (horizontal component). The gain is the filter's beta: larger
 * trusts the accelerometer and compass more and the gyro less.
 */
class Madgwick {
  public:
    void setGain(float beta) { _beta = beta; }
    float gain() const { return _beta; }
    void reset() { _q = {1, 0, 0, 0}; }
    const Quat &quaternion() const { return _q; }

    void update(const Vec3 &gyro, const Vec3 &accel, const Vec3 &mag, float dt) {
        if (mag[0] == 0 && mag[1] == 0 && mag[2] == 0) {
            updateImu(gyro, accel, dt);
            return;
        }
        float q0 = _q[0], q1 = _q[1], q2 = _q[2], q3 = _q[3];
        const float gx = gyro[0], gy = gyro[1], gz = gyro[2];
        float qDot0 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
        float qDot1 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
        float qDot2 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
        float qDot3 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

        const float accelNorm = norm(accel);
        if (accelNorm > 0) {
            const float ax = accel[0] / accelNorm, ay = accel[1] / accelNorm, az = accel[2] / accelNorm;
            const float magNorm = norm(mag);
            const float mx = mag[0] / magNorm, my = mag[1] / magNorm, mz = mag[2] / magNorm;

            const float _2q0mx = 2 * q0 * mx, _2q0my = 2 * q0 * my, _2q0mz = 2 * q0 * mz, _2q1mx = 2 * q1 * mx;
            const float _2q0 = 2 * q0, _2q1 = 2 * q1, _2q2 = 2 * q2, _2q3 = 2 * q3;
            const float _2q0q2 = 2 * q0 * q2, _2q2q3 = 2 * q2 * q3;
            const float q0q0 = q0 * q0, q0q1 = q0 * q1, q0q2 = q0 * q2, q0q3 = q0 * q3, q1q1 = q1 * q1,
                        q1q2 = q1 * q2, q1q3 = q1 * q3, q2q2 = q2 * q2, q2q3 = q2 * q3, q3q3 = q3 * q3;

            // The measured field in the world frame, reduced to its horizontal (bx) and vertical (bz) parts.
            const float hx = mx * q0q0 - _2q0my * q3 + _2q0mz * q2 + mx * q1q1 + _2q1 * my * q2 + _2q1 * mz * q3 -
                             mx * q2q2 - mx * q3q3;
            const float hy = _2q0mx * q3 + my * q0q0 - _2q0mz * q1 + _2q1mx * q2 - my * q1q1 + my * q2q2 +
                             _2q2 * mz * q3 - my * q3q3;
            const float _2bx = std::sqrt(hx * hx + hy * hy);
            const float _2bz = -_2q0mx * q2 + _2q0my * q1 + mz * q0q0 + _2q1mx * q3 - mz * q1q1 + _2q2 * my * q3 -
                               mz * q2q2 + mz * q3q3;
            const float _4bx = 2 * _2bx, _4bz = 2 * _2bz;

            const float fx = 2 * q1q3 - _2q0q2 - ax;
            const float fy = 2 * q0q1 + _2q2q3 - ay;
            const float fz = 1 - 2 * q1q1 - 2 * q2q2 - az;
            const float fmx = _2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx;
            const float fmy = _2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my;
            const float fmz = _2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz;

            float s0 = -_2q2 * fx + _2q1 * fy - _2bz * q2 * fmx + (-_2bx * q3 + _2bz * q1) * fmy + _2bx * q2 * fmz;
            float s1 = _2q3 * fx + _2q0 * fy - 4 * q1 * fz + _2bz * q3 * fmx + (_2bx * q2 + _2bz * q0) * fmy +
                       (_2bx * q3 - _4bz * q1) * fmz;
            float s2 = -_2q0 * fx + _2q3 * fy - 4 * q2 * fz + (-_4bx * q2 - _2bz * q0) * fmx +
                       (_2bx * q1 + _2bz * q3) * fmy + (_2bx * q0 - _4bz * q2) * fmz;
            float s3 = _2q1 * fx + _2q2 * fy + (-_4bx * q3 + _2bz * q1) * fmx + (-_2bx * q0 + _2bz * q2) * fmy +
                       _2bx * q1 * fmz;
            applyStep(s0, s1, s2, s3, qDot0, qDot1, qDot2, qDot3);
        }
        integrate(qDot0, qDot1, qDot2, qDot3, dt);
    }

    void updateImu(const Vec3 &gyro, const Vec3 &accel, float dt) {
        const float q0 = _q[0], q1 = _q[1], q2 = _q[2], q3 = _q[3];
        const float gx = gyro[0], gy = gyro[1], gz = gyro[2];
        float qDot0 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
        float qDot1 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
        float qDot2 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
        float qDot3 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

        const float accelNorm = norm(accel);
        if (accelNorm > 0) {
            const float ax = accel[0] / accelNorm, ay = accel[1] / accelNorm, az = accel[2] / accelNorm;
            const float _2q0 = 2 * q0, _2q1 = 2 * q1, _2q2 = 2 * q2, _2q3 = 2 * q3;
            const float _4q0 = 4 * q0, _4q1 = 4 * q1, _4q2 = 4 * q2, _8q1 = 8 * q1, _8q2 = 8 * q2;
            const float q0q0 = q0 * q0, q1q1 = q1 * q1, q2q2 = q2 * q2, q3q3 = q3 * q3;
            float s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
            float s1 = _4q1 * q3q3 - _2q3 * ax + 4 * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
            float s2 = 4 * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
            float s3 = 4 * q1q1 * q3 - _2q1 * ax + 4 * q2q2 * q3 - _2q2 * ay;
            applyStep(s0, s1, s2, s3, qDot0, qDot1, qDot2, qDot3);
        }
        integrate(qDot0, qDot1, qDot2, qDot3, dt);
    }

  private:
    Quat _q {1, 0, 0, 0};
    float _beta = 0.1f;

    static float norm(const Vec3 &v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

    // A zero gradient means the estimate already agrees with the measurement; normalising it would divide by zero.
    void applyStep(float s0, float s1, float s2, float s3, float &qDot0, float &qDot1, float &qDot2,
                   float &qDot3) const {
        const float stepNorm = std::sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
        if (stepNorm <= 0) return;
        qDot0 -= _beta * s0 / stepNorm;
        qDot1 -= _beta * s1 / stepNorm;
        qDot2 -= _beta * s2 / stepNorm;
        qDot3 -= _beta * s3 / stepNorm;
    }

    void integrate(float qDot0, float qDot1, float qDot2, float qDot3, float dt) {
        Quat q = {_q[0] + qDot0 * dt, _q[1] + qDot1 * dt, _q[2] + qDot2 * dt, _q[3] + qDot3 * dt};
        const float qNorm = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (qNorm <= 0) return;
        _q = {q[0] / qNorm, q[1] / qNorm, q[2] / qNorm, q[3] / qNorm};
    }
};
