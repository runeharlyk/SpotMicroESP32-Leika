#pragma once

// Baseline gait the residual policy was trained on: C++ port of the trot path of
// simulation/src/robot/firmware_gait.py (analytic_gait_action, GaitController.advance_phase,
// GaitController.generate_feet), as driven by QuadrupedMjEnv._action_to_joints.
// This is NOT walk_state.h: the sim retargeted that gait to spot_pico (fixed step lengths, a
// calibrated turn rate, no smoothing), and the policy only reproduces its training when it sits on
// exactly this baseline. esp32/test/test_spot_pico checks it against golden vectors from the Python.

#include <cmath>
#include <spot_pico/kinematics.h>

namespace spot_pico {

// Command -> gait coefficients. The trained values are baked into the exported policy header,
// because optimize_gait.py retunes them per run.
struct GaitCoef {
    float gain_x, gain_y, gain_yaw;
    float speed_base, speed_slope;
    float step_height, step_depth;
};

constexpr float TROT_OFFSET[LEG_COUNT] = {0.f, 0.5f, 0.5f, 0.f};  // fr, fl, rr, rl
constexpr float TROT_STAND_FRAC = 0.75f;
constexpr float TROT_SPEED_FACTOR = 2.f;

constexpr float MAX_STEP_LENGTH = 0.030f;
constexpr float MAX_LATERAL_STEP = 0.018f;
constexpr float MAX_TURN_STEP = 0.026f;

constexpr float BEZIER_COMBINATORIAL[12] = {1, 11, 55, 165, 330, 462, 462, 330, 165, 55, 11, 1};
constexpr float BEZIER_STEPS[12] = {-1.0f, -1.4f, -1.5f, -1.5f, -1.5f, 0.0f, 0.0f, 0.0f, 1.5f, 1.5f, 1.4f, 1.0f};
constexpr float BEZIER_HEIGHTS[12] = {0.0f, 0.0f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 1.1f, 1.1f, 1.1f, 0.0f, 0.0f};

struct GaitParams {
    float step_x {0}, step_z {0}, step_angle {0};
    float step_velocity {0.5f};
    float step_height {0}, step_depth {0};
};

inline float clip(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// cmd = [vx forward (m/s), vy left (m/s), yaw rate CCW (rad/s)].
inline void analyticGait(const float cmd[3], const GaitCoef &c, GaitParams &gait) {
    const float vx = cmd[0], vy = cmd[1], yaw = cmd[2];
    gait.step_x = clip(vx / c.gain_x, -1.f, 1.f) * MAX_STEP_LENGTH;
    gait.step_z = clip(vy / c.gain_y, -1.f, 1.f) * MAX_LATERAL_STEP;
    gait.step_angle = clip(-yaw / c.gain_yaw, -1.f, 1.f);
    const float speed = std::hypot(vx, vy) + std::fabs(yaw) * 0.1f;
    gait.step_velocity = clip(c.speed_base + c.speed_slope * speed, 0.f, 1.f);
    gait.step_height = c.step_height;
    gait.step_depth = c.step_depth;
}

inline float advancePhase(float phase, const GaitParams &gait, float dt) {
    const float velocity = gait.step_velocity > 0.5f ? gait.step_velocity : 0.5f;
    return std::fmod(phase + dt * velocity * TROT_SPEED_FACTOR, 1.f);
}

namespace detail {

inline void stanceCurve(float length, float angle, float depth, float phase, float point[3]) {
    const float step = length * (1.f - 2.f * phase);
    point[0] += step * std::cos(angle);
    point[2] += step * std::sin(angle);
    if (length != 0.f) point[1] = -depth * std::cos(((float)M_PI * (point[0] + point[2])) / (2.f * length));
}

inline void bezierCurve(float length, float angle, float height, float phase, float point[3]) {
    const float x_polar = std::cos(angle), z_polar = std::sin(angle);
    const float t = clip(phase, 1e-4f, 1.f - 1e-4f);
    const float one_minus = 1.f - t;
    float phase_power = 1.f;
    float inv_phase_power = std::pow(one_minus, 11.f);
    for (int i = 0; i < 12; ++i) {
        const float b = BEZIER_COMBINATORIAL[i] * phase_power * inv_phase_power;
        point[0] += b * BEZIER_STEPS[i] * length * x_polar;
        point[2] += b * BEZIER_STEPS[i] * length * z_polar;
        point[1] += b * BEZIER_HEIGHTS[i] * height;
        phase_power *= t;
        inv_phase_power /= one_minus;
    }
}

// Yaw command -> rotation rate: a foot's tangential half-stroke is turnRate() * radius / 2,
// calibrated so the mean over the stance feet lands on MAX_TURN_STEP (firmware_gait.TURN_RATE).
inline float turnRate() {
    static const float rate = [] {
        float radius_sum = 0.f;
        for (int leg = 0; leg < LEG_COUNT; ++leg) radius_sum += std::hypot(stanceFoot(leg, 0), stanceFoot(leg, 1));
        return 2.f * MAX_TURN_STEP / (radius_sum / LEG_COUNT);
    }();
    return rate;
}

// Rigid-body velocity at a foot's stance position (firmware_gait._stroke): the commanded
// translation plus omega x r, as a stride amplitude and heading in the curve's stride frame.
inline void stroke(const GaitParams &gait, const float foot[3], float &amplitude, float &heading) {
    const float turn = gait.step_angle * turnRate();
    const float sx = gait.step_z + turn * foot[1];
    const float sy = -gait.step_x - turn * foot[0];
    amplitude = std::hypot(sx, sy);
    heading = std::atan2(sx, -sy);
}

} // namespace detail

// Base-frame foot targets at `phase` (does not advance it).
inline void generateFeet(const GaitParams &gait, float phase, float feet[LEG_COUNT][3]) {
    const bool moving =
        std::fabs(gait.step_x) > 1e-6f || std::fabs(gait.step_z) > 1e-6f || std::fabs(gait.step_angle) > 1e-6f;

    for (int i = 0; i < LEG_COUNT; ++i) {
        float home[3];
        defaultFoot(i, home);
        const float leg_phase = std::fmod(phase + TROT_OFFSET[i], 1.f);
        const bool contact = leg_phase <= TROT_STAND_FRAC;
        const float ph = contact ? leg_phase / TROT_STAND_FRAC : (leg_phase - TROT_STAND_FRAC) / (1.f - TROT_STAND_FRAC);
        auto curve = contact ? detail::stanceCurve : detail::bezierCurve;
        const float amp = contact ? gait.step_depth : gait.step_height;

        float amplitude, heading;
        detail::stroke(gait, home, amplitude, heading);
        float delta[3] = {0, 0, 0};
        curve(amplitude * 0.5f, heading, amp, ph, delta);

        // stride frame [along, vertical, cross] -> base frame: forward -> -Y, cross -> +X, lift -> +Z
        feet[i][0] = home[0] + delta[2];
        feet[i][1] = moving ? home[1] - delta[0] : home[1];
        feet[i][2] = moving ? home[2] + delta[1] : home[2];
    }
}

} // namespace spot_pico
