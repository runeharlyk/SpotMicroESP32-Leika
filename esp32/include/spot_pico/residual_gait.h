#pragma once

// Baseline gait the residual policy was trained on: C++ port of the trot path of
// simulation/src/robot/firmware_gait.py (analytic_gait_action, gait_action, GaitController.step and generate_feet),
// as driven by QuadrupedMjEnv._action_to_joints. That is walk_state.h's trot with the MINI's constants at the client's
// height, speed and step height, its command smoothing, its joint speed cap and stop, and its reach bound, with each
// foot placed relative to the Pico's leg as the firmware places it relative to the MINI's. The policy only reproduces
// its training on exactly this baseline; esp32/test/host/spot_pico_test.cpp checks it against golden vectors from the
// Python.

#include <cmath>
#include <kinematics.h>
#include <spot_pico/kinematics.h>

namespace spot_pico {

// The client's speed and step height commands (speed_model.SPEED and STEP_HEIGHT), as handleCommand turns them into
// the gait's cadence and lift.
constexpr float SPEED = 0.5f;
constexpr float STEP_HEIGHT = 0.5f * MINI.max_step_height;
constexpr float STEP_DEPTH = 0.002f;

constexpr float TROT_OFFSET[LEG_COUNT] = {0.5f, 0.f, 0.f, 0.5f};  // fr, fl, rr, rl: walk_state.h's fl, fr, rl, rr
constexpr float TROT_STAND_FRAC = 0.75f;
constexpr float TROT_SPEED_FACTOR = 2.f;

// MotionState's smoothing: a step in the stroke settles to 95 % in a third of a second, without overshoot.
constexpr float SMOOTHING_OMEGA = 4.744f / 0.333f;
// walk_state.h's cap on joint speed and the parts of a tick it slows by, and the bends it keeps every knee between.
constexpr float MAX_JOINT_SPEED_DEG_S = 0.75f * 720.f;
constexpr int SLOWDOWN_PARTS = 64;
constexpr float MIN_KNEE_BEND_DEG = 10.f, MAX_KNEE_BEND_DEG = 148.f;

constexpr float BEZIER_COMBINATORIAL[12] = {1, 11, 55, 165, 330, 462, 462, 330, 165, 55, 11, 1};
constexpr float BEZIER_STEPS[12] = {-1.0f, -1.4f, -1.5f, -1.5f, -1.5f, 0.0f, 0.0f, 0.0f, 1.5f, 1.5f, 1.4f, 1.0f};
constexpr float BEZIER_HEIGHTS[12] = {0.0f, 0.0f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 1.1f, 1.1f, 1.1f, 0.0f, 0.0f};
constexpr int SWING_SAMPLES = 64;

/** The stroke the gait walks (forward, left in metres, and the turn) and follows the command with, and its phase. */
struct Gait {
    float steps[3] {0, 0, 0};
    float target[3] {0, 0, 0};
    float smoothing[3] {0, 0, 0};
    float phase {0};
};

inline float clip(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/** Sticks (lx to the right, ly forward, rx turning left) to the stroke the gait smooths towards (gait_action). */
inline void gaitAction(const float sticks[3], Gait &gait) {
    gait.target[0] = sticks[1] * MINI.max_step_length;
    gait.target[1] = -sticks[0] * MINI.max_step_length;
    gait.target[2] = sticks[2];
}

/**
 * cmd = [vx forward (m/s), vy left (m/s), yaw rate CCW (rad/s)] to the sticks that command it at the trot's nominal
 * stroke and cadence (analytic_gait_action), before the slowdown and the reach bound.
 */
inline void nominalSticks(const float cmd[3], float sticks[3]) {
    const float rate = std::fmax(SPEED, 0.5f) * TROT_SPEED_FACTOR / TROT_STAND_FRAC;
    const float linear = MINI.max_step_length * rate;
    sticks[0] = clip(-cmd[1] / linear, -1.f, 1.f);
    sticks[1] = clip(cmd[0] / linear, -1.f, 1.f);
    sticks[2] = clip(cmd[2] / rate, -1.f, 1.f);
}

namespace detail {

inline float bezierAt(const float (&points)[12], float phase) {
    float value = 0.f;
    for (int i = 0; i < 12; ++i)
        value += BEZIER_COMBINATORIAL[i] * std::pow(phase, (float)i) * std::pow(1.f - phase, (float)(11 - i)) * points[i];
    return value;
}

struct Swing {
    float overshoot;
    float along[SWING_SAMPLES + 1], lift[SWING_SAMPLES + 1];
    float reach_out_sq, reach_in_sq;
};

// The swing's furthest reach along the stroke in half strokes, the swing at SWING_SAMPLES + 1 phases, and the MINI's
// reach spheres about each leg's mount (walk_state.h's configure).
inline const Swing &swing() {
    static const Swing table = [] {
        Swing t {};
        t.overshoot = 0.f;
        for (int sample = 0; sample <= 1024; ++sample)
            t.overshoot = std::fmax(t.overshoot, std::fabs(bezierAt(BEZIER_STEPS, sample / 1024.f)));
        for (int sample = 0; sample <= SWING_SAMPLES; ++sample) {
            t.along[sample] = bezierAt(BEZIER_STEPS, (float)sample / SWING_SAMPLES);
            t.lift[sample] = bezierAt(BEZIER_HEIGHTS, (float)sample / SWING_SAMPLES);
        }
        const auto femur_to_foot = [](float knee_bend_deg) {
            return std::sqrt(MINI.femur * MINI.femur + MINI.tibia * MINI.tibia +
                             2 * MINI.femur * MINI.tibia * std::cos(knee_bend_deg * DEG2RAD_F));
        };
        const float outer = femur_to_foot(MIN_KNEE_BEND_DEG);
        const float inner = femur_to_foot(MAX_KNEE_BEND_DEG) + MINI.coxa_offset;
        t.reach_out_sq = MINI.coxa * MINI.coxa + outer * outer;
        t.reach_in_sq = MINI.coxa * MINI.coxa + inner * inner;
        return t;
    }();
    return table;
}

inline float side(int leg) { return footLateralOffset(leg) > 0 ? 1.f : -1.f; }

// The firmware's default feet about its body centre in the base frame, where a turn takes each foot's stroke.
inline float firmwareFoot(int leg, int axis) {
    return axis == 0 ? side(leg) * (MINI.W / 2 + MINI.coxa) : (LEG_GEOMETRY[leg].hip[1] > 0 ? 1.f : -1.f) * MINI.L / 2;
}

// A foot's stroke in the base frame (X left, Y rear): the translation and the rotation about the body centre.
inline void strokeVector(const Gait &gait, int leg, float &sx, float &sy) {
    sx = gait.steps[1] - gait.steps[2] * firmwareFoot(leg, 1);
    sy = -gait.steps[0] + gait.steps[2] * firmwareFoot(leg, 0);
}

/** walk_state.h's boundToReach through the MINI's legs (_reach_bound): the share of the stroke, and the step height. */
inline void reachBound(const Gait &gait, float &scale, float &step_height) {
    const Swing &s = swing();
    const float coxa = MINI.coxa, deepest = BODY_HEIGHT + STEP_DEPTH;
    const float room = std::sqrt(std::fmax(0.f, s.reach_out_sq - deepest * deepest));
    const float least_offset = std::sqrt(std::fmax(0.f, s.reach_in_sq - BODY_HEIGHT * BODY_HEIGHT));
    scale = coxa < room && least_offset <= coxa ? 1.f : 0.f;
    float half[LEG_COUNT], outward[LEG_COUNT];
    for (int leg = 0; leg < LEG_COUNT; ++leg) {
        float sx, sy;
        strokeVector(gait, leg, sx, sy);
        const float stroke = std::hypot(sx, sy);
        half[leg] = stroke / 2;
        const float reach = s.overshoot * half[leg];
        outward[leg] = stroke > 0 ? side(leg) * sx / stroke : 0.f;
        const float a = reach * reach, b = coxa * std::fabs(outward[leg]) * reach, c = coxa * coxa - room * room;
        const float discriminant = b * b - a * c;
        if (c < 0 && reach > 0 && discriminant >= 0) scale = std::fmin(scale, -c / (b + std::sqrt(discriminant)));
        const float inward = std::fabs(outward[leg]) * reach;
        if (least_offset > 0 && inward > 0) scale = std::fmin(scale, (coxa - least_offset) / inward);
    }
    step_height = STEP_HEIGHT;
    for (int leg = 0; leg < LEG_COUNT; ++leg)
        for (int sample = 1; sample < SWING_SAMPLES; ++sample) {
            const float offset = coxa + scale * half[leg] * s.along[sample] * outward[leg];
            const float clear = std::sqrt(std::fmax(0.f, s.reach_in_sq - (offset > 0 ? offset * offset : 0.f)));
            step_height = std::fmin(step_height, std::fmax(0.f, BODY_HEIGHT - clear) / s.lift[sample]);
        }
}

inline void stanceCurve(float length, float angle, float depth, float phase, float point[3]) {
    const float step = length * (1.f - 2.f * phase);
    point[0] += step * std::cos(angle);
    point[2] += step * std::sin(angle);
    if (length != 0.f) point[1] = -depth * std::cos(((float)M_PI * step) / (2.f * length));
}

inline void bezierCurve(float length, float angle, float height, float phase, float point[3]) {
    const float x_polar = std::cos(angle), z_polar = std::sin(angle);
    float phase_powers[12], rest_powers[12];
    phase_powers[0] = rest_powers[0] = 1.f;
    for (int i = 1; i < 12; ++i) {
        phase_powers[i] = phase_powers[i - 1] * phase;
        rest_powers[i] = rest_powers[i - 1] * (1.f - phase);
    }
    for (int i = 0; i < 12; ++i) {
        const float b = BEZIER_COMBINATORIAL[i] * phase_powers[i] * rest_powers[11 - i];
        point[0] += b * BEZIER_STEPS[i] * length * x_polar;
        point[2] += b * BEZIER_STEPS[i] * length * z_polar;
        point[1] += b * BEZIER_HEIGHTS[i] * height;
    }
}

inline bool moving(const Gait &gait) {
    return std::fabs(gait.steps[0]) >= 0.001f || std::fabs(gait.steps[1]) >= 0.001f ||
           std::fabs(gait.steps[2]) >= 0.001f;
}

inline bool swinging(const Gait &gait) {
    for (int leg = 0; leg < LEG_COUNT; ++leg)
        if (std::fmod(gait.phase + TROT_OFFSET[leg], 1.f) > TROT_STAND_FRAC) return true;
    return false;
}

// The stroke follows the command, then the phase advances; a stopped walk holds its phase once every foot is down.
inline void advance(Gait &gait, float dt) {
    const float decay = std::exp(-SMOOTHING_OMEGA * dt);
    for (int k = 0; k < 3; ++k) {
        const float offset = gait.steps[k] - gait.target[k];
        const float drive = gait.smoothing[k] + SMOOTHING_OMEGA * offset;
        gait.smoothing[k] = (gait.smoothing[k] - SMOOTHING_OMEGA * drive * dt) * decay;
        gait.steps[k] = gait.target[k] + (offset + drive * dt) * decay;
    }
    if (!moving(gait) && !swinging(gait)) return;
    gait.phase = std::fmod(gait.phase + dt * std::fmax(SPEED, 0.5f) * TROT_SPEED_FACTOR, 1.f);
}

} // namespace detail

/** Base-frame foot targets at the gait's phase and stroke (does not advance it). */
inline void generateFeet(const Gait &gait, float feet[LEG_COUNT][3]) {
    float scale, step_height;
    detail::reachBound(gait, scale, step_height);
    for (int i = 0; i < LEG_COUNT; ++i) {
        defaultFoot(i, feet[i]);
        const float leg_phase = std::fmod(gait.phase + TROT_OFFSET[i], 1.f);
        float sx, sy;
        detail::strokeVector(gait, i, sx, sy);
        const float length = scale * std::hypot(sx, sy) * 0.5f, heading = std::atan2(sx, -sy);
        float delta[3] = {0, 0, 0};
        if (leg_phase <= TROT_STAND_FRAC)
            detail::stanceCurve(length, heading, STEP_DEPTH, leg_phase / TROT_STAND_FRAC, delta);
        else
            detail::bezierCurve(length, heading, step_height, (leg_phase - TROT_STAND_FRAC) / (1.f - TROT_STAND_FRAC),
                                delta);
        // stride frame [along, vertical, cross] -> base frame: forward -> -Y, cross -> +X, lift -> +Z
        feet[i][0] += delta[2];
        feet[i][1] -= delta[0];
        feet[i][2] += delta[1];
    }
}

/**
 * The MINI's body for these spot_pico feet: level BODY_HEIGHT above them, each firmware foot displaced from its default
 * as the spot_pico foot is from its stance (spot_pico +X left, +Y rear, +Z up to x forward, y up, z left).
 */
inline void miniBody(const float feet[LEG_COUNT][3], body_state_t &body) {
    body.omega = body.phi = body.psi = body.xm = body.zm = 0;
    body.ym = BODY_HEIGHT;
    for (int fw = 0; fw < LEG_COUNT; ++fw) {
        const int leg = FIRMWARE_TO_SIM_LEG[fw];
        body.feet[fw][0] = MINI.default_feet_positions[fw][0] - (feet[leg][1] - stanceFoot(leg, 1));
        body.feet[fw][1] = MINI.default_feet_positions[fw][1] + (feet[leg][2] - stanceFoot(leg, 2));
        body.feet[fw][2] = MINI.default_feet_positions[fw][2] + (feet[leg][0] - stanceFoot(leg, 0));
        body.feet[fw][3] = 1;
    }
}

namespace detail {

// The firmware's joint angles (degrees) for the feet at the gait's state: what walk_state.h caps the speed of.
inline void firmwareJointAngles(const Gait &gait, float degrees[12]) {
    float feet[LEG_COUNT][3];
    generateFeet(gait, feet);
    body_state_t body;
    miniBody(feet, body);
    Kinematics(MINI).calculate_inverse_kinematics(body, degrees);
}

} // namespace detail

/**
 * One tick (GaitController.step): the stroke follows the command and the phase advances, by dt or by the most 64ths
 * of it the firmware's joints can follow at MAX_JOINT_SPEED_DEG_S; a jump not even a 64th keeps under the cap is
 * walked at full time.
 */
inline void step(Gait &gait, float dt) {
    const Gait start = gait;
    float before[12];
    detail::firmwareJointAngles(gait, before);
    const auto followable = [&](int parts) {
        gait = start;
        detail::advance(gait, dt * parts / SLOWDOWN_PARTS);
        float after[12];
        detail::firmwareJointAngles(gait, after);
        for (int joint = 0; joint < 12; ++joint)
            if (std::fabs(after[joint] - before[joint]) > MAX_JOINT_SPEED_DEG_S * dt) return false;
        return true;
    };
    if (followable(SLOWDOWN_PARTS)) return;
    int fast_enough = 0, too_fast = SLOWDOWN_PARTS;
    while (too_fast - fast_enough > 1) {
        const int parts = (fast_enough + too_fast) / 2;
        (followable(parts) ? fast_enough : too_fast) = parts;
    }
    followable(fast_enough > 0 ? fast_enough : SLOWDOWN_PARTS);
}

} // namespace spot_pico
