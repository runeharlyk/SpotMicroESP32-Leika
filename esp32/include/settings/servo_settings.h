#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <template/state_result.h>
#include <platform_shared/api.pb.h>
#include <peripherals/servo_output.h>

using ServoSettings = api_ServoSettings;

constexpr uint32_t PCA9685_CHANNELS = 16;

inline ServoSettings ServoSettings_defaults() {
    ServoSettings settings = api_ServoSettings_init_zero;
    settings.servos_count = SERVO_COUNT;
    for (size_t i = 0; i < SERVO_COUNT; i++) {
        settings.servos[i].center_pwm = 306;
        std::snprintf(settings.servos[i].name, sizeof(settings.servos[i].name), "Servo%u", unsigned(i + 1));
    }
    return settings;
}

/** The channel that drives a joint: the stored map, or joint j on channel j when there is none. */
inline uint32_t jointChannel(const ServoSettings &settings, size_t joint) {
    return settings.channels_count == SERVO_COUNT ? settings.channels[joint] : joint;
}

/** Twelve finite centre PWMs inside the servo range, and either no channel map or twelve distinct channels 0-15. */
inline bool validServoSettings(const ServoSettings &settings) {
    if (settings.servos_count != SERVO_COUNT) return false;
    for (size_t i = 0; i < SERVO_COUNT; i++) {
        const float pwm = settings.servos[i].center_pwm;
        if (!isFinite(pwm) || pwm < SERVO_PWM_MIN || pwm > SERVO_PWM_MAX) return false;
    }
    if (settings.channels_count == 0) return true;
    if (settings.channels_count != SERVO_COUNT) return false;
    bool used[PCA9685_CHANNELS] = {};
    for (size_t i = 0; i < SERVO_COUNT; i++) {
        const uint32_t channel = settings.channels[i];
        if (channel >= PCA9685_CHANNELS || used[channel]) return false;
        used[channel] = true;
    }
    return true;
}

/** The stored settings, with the variant's joint model the app draws from. */
inline void ServoSettings_read(const ServoSettings &settings, ServoSettings &proto) {
    proto = settings;
    proto.has_model = true;
    std::copy(std::begin(VARIANT_JOINT_MODEL.direction), std::end(VARIANT_JOINT_MODEL.direction),
              proto.model.direction);
    std::copy(std::begin(VARIANT_JOINT_MODEL.center_angle), std::end(VARIANT_JOINT_MODEL.center_angle),
              proto.model.center_angle);
    proto.model.pwm_per_degree = VARIANT_JOINT_MODEL.pwm_per_degree;
}

// The model is the variant's and is not stored; a save without channels, from an app that does not know them, keeps
// the wiring in force.
inline StateUpdateResult ServoSettings_update(const ServoSettings &proto, ServoSettings &settings) {
    if (!validServoSettings(proto)) return StateUpdateResult::ERROR;
    const ServoSettings previous = settings;
    settings = proto;
    settings.has_model = false;
    settings.model = api_JointModel_init_zero;
    if (proto.channels_count == 0) {
        settings.channels_count = previous.channels_count;
        std::copy(std::begin(previous.channels), std::end(previous.channels), settings.channels);
    }
    return StateUpdateResult::CHANGED;
}
