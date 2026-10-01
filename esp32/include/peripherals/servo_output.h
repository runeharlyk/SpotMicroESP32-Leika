#pragma once

#include <joint_model.h>
#include <platform_shared/api.pb.h>
#include <utils/finite.h>
#include <cstdint>

// The pulse widths (PCA9685 counts at 50 Hz) the servos accept; outside them they press on their stops.
constexpr uint16_t SERVO_PWM_MIN = 125;
constexpr uint16_t SERVO_PWM_MAX = 600;
constexpr size_t SERVO_COUNT = 12;

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

/** Settings the controller can drive safely: twelve servos, each with a direction of +-1 and a finite, sane calibration. */
inline bool validServoSettings(const api_ServoSettings &settings) {
    if (settings.servos_count != SERVO_COUNT) return false;
    for (size_t i = 0; i < SERVO_COUNT; i++) {
        const api_Servo &servo = settings.servos[i];
        if (servo.direction != 1 && servo.direction != -1) return false;
        if (!isFinite(servo.conversion) || servo.conversion <= 0 || servo.conversion > 10) return false;
        if (!isFinite(servo.center_pwm) || servo.center_pwm < SERVO_PWM_MIN || servo.center_pwm > SERVO_PWM_MAX)
            return false;
        if (!isFinite(servo.center_angle) || servo.center_angle < -180 || servo.center_angle > 180) return false;
    }
    return true;
}
