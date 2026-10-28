#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <template/state_result.h>
#include <platform_shared/api.pb.h>
#include <peripherals/servo_output.h>

using ServoSettings = api_ServoSettings;

constexpr uint32_t PCA9685_CHANNELS = 16;
constexpr float FACTORY_CENTER_PWM = 306;

inline ServoSettings ServoSettings_defaults() {
    ServoSettings settings = api_ServoSettings_init_zero;
    settings.servos_count = SERVO_COUNT;
    settings.supply_voltage = DEFAULT_SERVO_SUPPLY_VOLTS;
    for (size_t i = 0; i < SERVO_COUNT; i++) {
        settings.servos[i].center_pwm = FACTORY_CENTER_PWM;
        std::snprintf(settings.servos[i].name, sizeof(settings.servos[i].name), "Servo%u", unsigned(i + 1));
    }
    return settings;
}

/** Whether the robot's centres were calibrated: a robot still on the factory centres for every joint was not. */
inline bool servosCalibrated(const ServoSettings &settings) {
    for (size_t i = 0; i < settings.servos_count; i++) {
        if (settings.servos[i].center_pwm != FACTORY_CENTER_PWM) return true;
    }
    return false;
}

/** The volts the servos run at: the stored supply, or the default for a robot that never stored one. */
inline float servoSupplyVolts(const ServoSettings &settings) {
    return settings.supply_voltage > 0 ? settings.supply_voltage : DEFAULT_SERVO_SUPPLY_VOLTS;
}

/** The channel that drives a joint: the stored map, or joint j on channel j when there is none. */
inline uint32_t jointChannel(const ServoSettings &settings, size_t joint) {
    return settings.channels_count == SERVO_COUNT ? settings.channels[joint] : joint;
}

/**
 * Twelve finite centre PWMs inside the servo range, either no channel map or twelve distinct channels 0-15, and a
 * finite supply that is not negative.
 */
inline bool validServoSettings(const ServoSettings &settings) {
    if (settings.servos_count != SERVO_COUNT) return false;
    if (!isFinite(settings.supply_voltage) || settings.supply_voltage < 0) return false;
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

/** The stored settings, with the variant's joint model the app draws from; without one while no variant is chosen. */
inline void ServoSettings_read(const ServoSettings &settings, ServoSettings &proto, const JointModel *model) {
    proto = settings;
    proto.supply_voltage = servoSupplyVolts(settings);
    proto.has_model = model != nullptr;
    proto.model = api_JointModel_init_zero;
    if (!model) return;
    std::copy(std::begin(model->direction), std::end(model->direction), proto.model.direction);
    std::copy(std::begin(model->center_angle), std::end(model->center_angle), proto.model.center_angle);
    proto.model.pwm_per_degree = model->pwm_per_degree;
}

// The model is the variant's and is not stored; a save without channels or a supply, from an app that does not know
// them, keeps the wiring and the supply in force.
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
    if (proto.supply_voltage == 0) settings.supply_voltage = previous.supply_voltage;
    return StateUpdateResult::CHANGED;
}
