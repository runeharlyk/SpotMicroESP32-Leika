#pragma once

#include <joint_model.h>
#include <utils/finite.h>
#include <algorithm>
#include <cstdint>

// The pulse widths (PCA9685 counts at 50 Hz) the servos accept; outside them they press on their stops.
constexpr uint16_t SERVO_PWM_MIN = 125;
constexpr uint16_t SERVO_PWM_MAX = 600;
constexpr size_t SERVO_COUNT = 12;
// The MG92B's rated speed (unloaded) is 0.13 s per 60 degrees at 4.8 V (461 deg/s) and 0.08 s at 6 V (750 deg/s).
constexpr float MG92B_LOW_VOLTS = 4.8f, MG92B_HIGH_VOLTS = 6.0f;
constexpr float MG92B_LOW_SPEED_DEG_S = 60 / 0.13f, MG92B_HIGH_SPEED_DEG_S = 60 / 0.08f;
// The supply the servos are taken to run at until the robot's servo settings say otherwise.
constexpr float DEFAULT_SERVO_SUPPLY_VOLTS = MG92B_HIGH_VOLTS;
// How fast the servo controller turns a joint at the default supply: 180 degrees in 250 ms, a little under the rating.
constexpr float SERVO_MAX_SPEED_DEG_S = 720;

/** The MG92B's rated speed at this supply, linear between its two ratings and held at them outside. */
constexpr float mg92bRatedSpeed(float volts) {
    const float below = MG92B_HIGH_VOLTS - std::clamp(volts, MG92B_LOW_VOLTS, MG92B_HIGH_VOLTS);
    return MG92B_HIGH_SPEED_DEG_S -
           (MG92B_HIGH_SPEED_DEG_S - MG92B_LOW_SPEED_DEG_S) * below / (MG92B_HIGH_VOLTS - MG92B_LOW_VOLTS);
}

/** How fast the servo controller turns a joint at this supply: the rating, with the margin it keeps at 6 V. */
constexpr float servoSpeedLimit(float volts) {
    return mg92bRatedSpeed(volts) / MG92B_HIGH_SPEED_DEG_S * SERVO_MAX_SPEED_DEG_S;
}

inline uint16_t boundedPwm(float pwm) {
    return static_cast<uint16_t>(std::clamp(finiteOrZero(pwm), float(SERVO_PWM_MIN), float(SERVO_PWM_MAX)));
}

/**
 * The PWM for a joint's angle (degrees, servo space) through the variant's joint model and the robot's centre PWM,
 * clamped as a float before it becomes an integer: converting a negative or NaN float to unsigned is undefined, and
 * in practice wraps to the far end stop. A non-finite angle holds the joint's centre.
 */
inline uint16_t servoPwm(const JointModel &model, size_t joint, float centerPwm, float angle) {
    const float servoAngle = model.direction[joint] * finiteOrZero(angle) + model.center_angle[joint];
    return boundedPwm(servoAngle * model.pwm_per_degree + centerPwm);
}

/** One step toward the target, no larger than maxStep: a jump is spread out, a motion slower than that passes as is. */
inline float slewToward(float current, float target, float maxStep) {
    return current + std::clamp(target - current, -maxStep, maxStep);
}
