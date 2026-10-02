#pragma once

#include <joint_model.h>
#include <utils/finite.h>
#include <algorithm>
#include <cstdint>

// The pulse widths (PCA9685 counts at 50 Hz) the servos accept; outside them they press on their stops.
constexpr uint16_t SERVO_PWM_MIN = 125;
constexpr uint16_t SERVO_PWM_MAX = 600;
constexpr size_t SERVO_COUNT = 12;
// The MG92B's rated speed at 6 V is 0.08 s per 60 degrees (750 deg/s, unloaded): 180 degrees in 250 ms.
constexpr float SERVO_MAX_SPEED_DEG_S = 720;

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
