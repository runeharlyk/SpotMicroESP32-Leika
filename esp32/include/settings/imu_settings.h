#pragma once

#include <algorithm>
#include <cmath>
#include <platform_shared/api.pb.h>
#include <peripherals/imu/imu.h>
#include <utils/finite.h>

inline api_ImuSettings imuSettingsDefaults() {
    api_ImuSettings settings = api_ImuSettings_init_zero;
    std::copy(IDENTITY3.begin(), IDENTITY3.end(), settings.mounting);
    std::copy(IDENTITY3.begin(), IDENTITY3.end(), settings.mag_alignment);
    std::copy(IDENTITY3.begin(), IDENTITY3.end(), settings.mag_soft_iron);
    settings.fusion_gain = ImuConfig {}.fusionGain;
    return settings;
}

/** Settings saved before ImuSettings existed carry none: proto3 would read every number in them as zero. */
inline api_ImuSettings effectiveImuSettings(const api_PeripheralSettings &settings) {
    return settings.has_imu ? settings.imu : imuSettingsDefaults();
}

inline Mat3 toMat3(const float (&values)[9]) {
    Mat3 m;
    std::copy(values, values + 9, m.begin());
    return m;
}

inline bool validImuSettings(const api_ImuSettings &settings) {
    const Mat3 softIron = toMat3(settings.mag_soft_iron);
    const float det = softIron[0] * (softIron[4] * softIron[8] - softIron[5] * softIron[7]) -
                      softIron[1] * (softIron[3] * softIron[8] - softIron[5] * softIron[6]) +
                      softIron[2] * (softIron[3] * softIron[7] - softIron[4] * softIron[6]);
    const bool offsetFinite =
        std::all_of(std::begin(settings.mag_offset), std::end(settings.mag_offset), [](float v) { return isFinite(v); });
    return isRotation(toMat3(settings.mounting), 1e-3f) && isRotation(toMat3(settings.mag_alignment), 1e-3f) &&
           isFinite(det) && std::fabs(det) > 1e-3f && offsetFinite && settings.fusion_gain > 0 &&
           settings.fusion_gain <= 1;
}

// More tilt than this means the robot is not on a level surface, or the mounting is wrong.
constexpr float MAX_LEVEL_TILT_DEG = 15;

/**
 * Folds the tilt a still robot's accelerometer shows (`up`, in the body frame of the current mounting) into the
 * mounting, so the robot reads level. Above MAX_LEVEL_TILT_DEG it is refused and the settings stay as they were.
 */
inline bool levelImuSettings(api_ImuSettings &settings, const Vec3 &up, float &tiltDeg) {
    tiltDeg = tiltOf(up) / 0.01745329f;
    if (!(tiltDeg <= MAX_LEVEL_TILT_DEG)) return false;
    const Mat3 levelled = mul(levellingRotation(up), toMat3(settings.mounting));
    std::copy(levelled.begin(), levelled.end(), settings.mounting);
    return true;
}

inline ImuConfig imuConfigFrom(const api_ImuSettings &settings) {
    ImuConfig config;
    config.mounting = toMat3(settings.mounting);
    config.magAlignment = toMat3(settings.mag_alignment);
    config.magOffset = {settings.mag_offset[0], settings.mag_offset[1], settings.mag_offset[2]};
    config.magSoftIron = toMat3(settings.mag_soft_iron);
    config.fusionGain = settings.fusion_gain;
    return config;
}
