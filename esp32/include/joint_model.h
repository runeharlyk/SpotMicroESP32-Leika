#pragma once

#include <cstddef>

/**
 * How each joint's servo sits in a variant's mechanical design: the sign that turns a joint angle into the servo's
 * angle, the servo angle at which the joint is at its kinematic zero, and the servo model's PWM counts per degree.
 * A robot's own calibration is only the centre PWM of each joint (servo_output.h).
 */
struct JointModel {
    float direction[12];
    float center_angle[12];
    float pwm_per_degree;
};

constexpr JointModel JOINT_MODEL_SPOTMICRO_ESP32 = {
    {-1, 1, 1, -1, -1, -1, 1, 1, 1, 1, -1, -1}, {0, -45, 90, 0, 45, -90, 0, -45, 90, 0, 45, -90}, 2.0f};

// From the Pico's calibration on 2026-10-01: the hips turn the other way and the knees are mirrored.
constexpr JointModel JOINT_MODEL_SPOTMICRO_ESP32_MINI = {
    {1, 1, -1, 1, -1, 1, -1, 1, -1, -1, -1, 1}, {0, -45, -90, 0, 45, 90, 0, -45, -90, 0, 45, 90}, 2.0f};

// No Yertle calibration says otherwise yet.
constexpr JointModel JOINT_MODEL_SPOTMICRO_YERTLE = JOINT_MODEL_SPOTMICRO_ESP32;

#if defined(SPOTMICRO_ESP32_MINI)
constexpr const JointModel &VARIANT_JOINT_MODEL = JOINT_MODEL_SPOTMICRO_ESP32_MINI;
#elif defined(SPOTMICRO_YERTLE)
constexpr const JointModel &VARIANT_JOINT_MODEL = JOINT_MODEL_SPOTMICRO_YERTLE;
#else
constexpr const JointModel &VARIANT_JOINT_MODEL = JOINT_MODEL_SPOTMICRO_ESP32;
#endif
