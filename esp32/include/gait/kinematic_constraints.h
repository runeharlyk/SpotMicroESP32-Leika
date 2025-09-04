#pragma once

#include <kinematics.h>
#include <utils/math_utils.h>
#include <cmath>

class KinConfig {
  public:
#if defined(SPOTMICRO_ESP32)
    static constexpr float coxa = 60.5f / 100.0f;
    static constexpr float coxa_offset = 10.0f / 100.0f;
    static constexpr float femur = 111.2f / 100.0f;
    static constexpr float tibia = 118.5f / 100.0f;
    static constexpr float L = 207.5f / 100.0f;
    static constexpr float W = 78.0f / 100.0f;
#elif defined(SPOTMICRO_ESP32_MINI)
    static constexpr float coxa = 35.0f / 100.0f;
    static constexpr float coxa_offset = 0.0f / 100.0f;
    static constexpr float femur = 60.0f / 100.0f;
    static constexpr float tibia = 60.0f / 100.0f;
    static constexpr float L = 160.0f / 100.0f;
    static constexpr float W = 80.0f / 100.0f;
#elif defined(SPOTMICRO_YERTLE)
    static constexpr float coxa = 35.0f / 100.0f;
    static constexpr float coxa_offset = 0.0f;
    static constexpr float femur = 130.0f / 100.0f;
    static constexpr float tibia = 130.0f / 100.0f;
    static constexpr float L = 240.0f / 100.0f;
    static constexpr float W = 78.0f / 100.0f;
#endif

    static constexpr float mountOffsets[4][3] = {
        {L / 2, 0, W / 2}, {L / 2, 0, -W / 2}, {-L / 2, 0, W / 2}, {-L / 2, 0, -W / 2}};

    static constexpr float default_feet_positions[4][4] = {
        {mountOffsets[0][0], 0, mountOffsets[0][2] + coxa, 1},
        {mountOffsets[1][0], 0, mountOffsets[1][2] - coxa, 1},
        {mountOffsets[2][0], 0, mountOffsets[2][2] + coxa, 1},
        {mountOffsets[3][0], 0, mountOffsets[3][2] - coxa, 1},
    };

    // Max constants
    static constexpr float max_roll = 15 * (float)M_PI_2;
    static constexpr float max_pitch = 15 * (float)M_PI_2;

    static constexpr float max_body_shift_x = W / 3;
    static constexpr float max_body_shift_z = W / 3;

    static constexpr float max_leg_reach = femur + tibia - coxa_offset;

    static constexpr float min_body_height = max_leg_reach * 0.45;
    static constexpr float max_body_height = max_leg_reach * 0.9;
    static constexpr float body_height_range = max_body_height - min_body_height;

    static constexpr float max_step_length = 1;

    // Default constant
    static constexpr float default_step_depth = 0.002;
    static constexpr float default_body_height = min_body_height + body_height_range / 2;
    static constexpr float default_step_height = default_body_height / 2;
};
