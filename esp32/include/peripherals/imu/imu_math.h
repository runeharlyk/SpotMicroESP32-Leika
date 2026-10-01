#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

using Vec3 = std::array<float, 3>;
using Quat = std::array<float, 4>; // w, x, y, z: rotates body vectors into the world frame
using Mat3 = std::array<float, 9>; // row-major

constexpr Mat3 IDENTITY3 = {1, 0, 0, 0, 1, 0, 0, 0, 1};

namespace ImuValid {
constexpr uint32_t ACCEL = 1;
constexpr uint32_t GYRO = 2;
constexpr uint32_t MAG = 4;
constexpr uint32_t ORIENTATION = 8;
constexpr uint32_t YAW_DRIFTS = 16;
} // namespace ImuValid

/** One IMU reading in the body frame: x forward, y left, z up. */
struct ImuSample {
    int64_t t_us = 0;
    Vec3 accel {};          // m/s^2, specific force: +9.81 on z when level and still
    Vec3 gyro {};           // rad/s
    Vec3 mag {};            // microtesla
    int64_t mag_t_us = 0;
    Vec3 gravity {0, 0, -1};
    Quat quat {1, 0, 0, 0};
    Vec3 rpy {};            // roll, pitch, yaw (ZYX), rad
    float temperature = 0;  // degrees Celsius
    uint32_t valid = 0;     // ImuValid bits
};

inline Vec3 mul(const Mat3 &m, const Vec3 &v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

inline Mat3 mul(const Mat3 &a, const Mat3 &b) {
    Mat3 out {};
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++)
            for (int k = 0; k < 3; k++) out[row * 3 + col] += a[row * 3 + k] * b[k * 3 + col];
    return out;
}

inline Vec3 sub(const Vec3 &a, const Vec3 &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

inline Quat quatMul(const Quat &a, const Quat &b) {
    return {a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3], a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
            a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1], a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0]};
}

inline Quat quatConj(const Quat &q) { return {q[0], -q[1], -q[2], -q[3]}; }

/** The quaternion of a rotation matrix (Shepperd's method, stable for every rotation). */
inline Quat quatFromMatrix(const Mat3 &m) {
    const float trace = m[0] + m[4] + m[8];
    Quat q;
    if (trace > 0) {
        const float s = 2 * std::sqrt(1 + trace);
        q = {s / 4, (m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s};
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const float s = 2 * std::sqrt(1 + m[0] - m[4] - m[8]);
        q = {(m[7] - m[5]) / s, s / 4, (m[1] + m[3]) / s, (m[2] + m[6]) / s};
    } else if (m[4] > m[8]) {
        const float s = 2 * std::sqrt(1 + m[4] - m[0] - m[8]);
        q = {(m[2] - m[6]) / s, (m[1] + m[3]) / s, s / 4, (m[5] + m[7]) / s};
    } else {
        const float s = 2 * std::sqrt(1 + m[8] - m[0] - m[4]);
        q = {(m[3] - m[1]) / s, (m[2] + m[6]) / s, (m[5] + m[7]) / s, s / 4};
    }
    return q;
}

/** Gravity's direction in the body frame: the world's (0, 0, -1) rotated by the inverse of q. */
inline Vec3 gravityInBody(const Quat &q) {
    const float w = q[0], x = q[1], y = q[2], z = q[3];
    return {-2 * (x * z - w * y), -2 * (y * z + w * x), -(w * w - x * x - y * y + z * z)};
}

/** Roll, pitch and yaw (ZYX), with the simulation's formula. */
inline Vec3 rpyFromQuat(const Quat &q) {
    const float w = q[0], x = q[1], y = q[2], z = q[3];
    return {std::atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)),
            std::asin(std::clamp(2 * (w * y - z * x), -1.0f, 1.0f)),
            std::atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))};
}

/** The angle (radians) between a still body's measured up, its accelerometer, and the body's z axis. */
inline float tiltOf(const Vec3 &up) {
    const float length = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    return std::acos(std::clamp(up[2] / length, -1.0f, 1.0f));
}

/**
 * The smallest rotation that turns a still body's measured up onto its z axis (Rodrigues, about up x z): it corrects
 * roll and pitch and leaves the heading alone.
 */
inline Mat3 levellingRotation(const Vec3 &up) {
    const float length = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    const float x = up[0] / length, y = up[1] / length, c = up[2] / length;
    const float s2 = x * x + y * y;  // |up x z|^2, with up x z = (y, -x, 0)
    if (s2 < 1e-12f) return IDENTITY3;
    const float k = (1 - c) / s2;
    // I + [v]x + [v]x^2 * (1 - c) / |v|^2 for v = (y, -x, 0)
    return {1 - k * x * x, -k * x * y, -x, -k * x * y, 1 - k * y * y, -y, x, y, 1 - k * s2};
}

/** Whether m is a proper rotation: orthonormal with determinant +1, within `tolerance`. */
inline bool isRotation(const Mat3 &m, float tolerance) {
    const Mat3 t = {m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]};
    const Mat3 product = mul(m, t);
    for (int i = 0; i < 9; i++)
        if (std::fabs(product[i] - IDENTITY3[i]) > tolerance) return false;
    const float det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                      m[2] * (m[3] * m[7] - m[4] * m[6]);
    return std::fabs(det - 1) < tolerance;
}
