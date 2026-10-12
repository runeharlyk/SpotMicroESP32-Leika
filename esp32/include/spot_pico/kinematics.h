#pragma once

// Leika Mini (spot_pico) leg kinematics, as the residual policy was trained on them: C++ port of
// simulation/src/robot/firmware_gait.py (leg_ik, SpotPicoKinConfig, DEFAULT_FEET). Only WALK_NN uses it;
// every other mode runs the generic Mini geometry in kinematics.h. Keep both sides in lockstep;
// esp32/test/host/spot_pico_test.cpp checks this file against golden vectors from export_golden.py.
//
// Frame: spot_pico base_link, +X = left, +Y = rear, +Z = up. Leg order: 0 = fr, 1 = fl, 2 = rr, 3 = rl.
// Joint angles are radians, 0 = CAD stance pose, order (hip/abduction, femur, tibia).

#include <cmath>

namespace spot_pico {

constexpr int LEG_COUNT = 4;

struct LegGeometry {
    float hip[3];      // hip origin in the base frame
    float sy;          // hip axis sign (about Y)
    float p_femur[3];  // coxa -> femur offset
    float sf;          // femur axis sign (about X)
    float p_tibia[3];  // femur -> tibia offset
    float st;          // tibia axis sign (about X)
    float p_foot[3];   // tibia -> foot site offset
};

// Extracted from simulation/src/resources/spot_pico/scene.xml (same numbers as _LEG_GEOM).
constexpr LegGeometry LEG_GEOMETRY[LEG_COUNT] = {
    {{-0.039922f, -0.099805f, 0.000058f}, -1.f, {-0.0513f, 0.004305f, 0.f}, -1.f,
     {0.00215f, 0.043903f, -0.037783f}, -1.f, {0.006f, -0.037123f, -0.037123f}},
    {{0.040078f, -0.095155f, 0.000058f}, 1.f, {0.0516f, 0.004305f, 0.f}, -1.f,
     {-0.0143f, 0.043903f, -0.037783f}, 1.f, {0.006f, -0.037123f, -0.037123f}},
    {{-0.039922f, 0.077545f, 0.000058f}, -1.f, {-0.0513f, 0.004305f, 0.f}, -1.f,
     {0.00215f, 0.043903f, -0.037783f}, -1.f, {0.006f, -0.037123f, -0.037123f}},
    {{0.040078f, 0.077545f, 0.000058f}, 1.f, {0.0516f, 0.004305f, 0.f}, -1.f,
     {-0.0143f, 0.043903f, -0.037783f}, 1.f, {0.006f, -0.037123f, -0.037123f}},
};

// Nominal stance depth (m): feet rest this far below the hip.
constexpr float STANCE_DEPTH = 0.055f;

// Firmware legs are ordered FL, FR, RL, RR; spot_pico legs are fr, fl, rr, rl.
constexpr int FIRMWARE_TO_SIM_LEG[LEG_COUNT] = {1, 0, 3, 2};

// Foot lateral offset in the coxa frame; invariant under the femur and tibia rotations.
constexpr float footLateralOffset(int leg) {
    return LEG_GEOMETRY[leg].p_femur[0] + LEG_GEOMETRY[leg].p_tibia[0] + LEG_GEOMETRY[leg].p_foot[0];
}

// Default stance foot in the base frame: natural lateral offset, under the hip, STANCE_DEPTH below it.
constexpr float stanceFoot(int leg, int axis) {
    return axis == 0   ? LEG_GEOMETRY[leg].hip[0] + footLateralOffset(leg)
           : axis == 1 ? LEG_GEOMETRY[leg].hip[1]
                       : LEG_GEOMETRY[leg].hip[2] - STANCE_DEPTH;
}

inline void defaultFoot(int leg, float out[3]) {
    for (int axis = 0; axis < 3; ++axis) out[axis] = stanceFoot(leg, axis);
}

// Per-leg constants for the analytic IK, derived from LEG_GEOMETRY.
struct LegConfig {
    float x_off;  // foot lateral offset in the coxa frame (invariant under the X rotations)
    float o0[2];  // planar arm base in the coxa (y, z) plane
    float L1, L2; // femur and tibia link lengths
    float a1, a2; // rest angles of the two links
};

inline const LegConfig &legConfig(int leg) {
    static const auto configs = [] {
        struct Table {
            LegConfig legs[LEG_COUNT];
        } t {};
        for (int i = 0; i < LEG_COUNT; ++i) {
            const LegGeometry &g = LEG_GEOMETRY[i];
            LegConfig &c = t.legs[i];
            c.x_off = footLateralOffset(i);
            c.o0[0] = g.p_femur[1];
            c.o0[1] = g.p_femur[2];
            c.L1 = std::hypot(g.p_tibia[1], g.p_tibia[2]);
            c.L2 = std::hypot(g.p_foot[1], g.p_foot[2]);
            c.a1 = std::atan2(g.p_tibia[2], g.p_tibia[1]);
            c.a2 = std::atan2(g.p_foot[2], g.p_foot[1]);
        }
        return t;
    }();
    return configs.legs[leg];
}

// Analytic IK (knee-down branch): base-frame foot target -> (q_hip, q_femur, q_tibia).
// Unreachable targets are clamped to the workspace boundary, as in the Python.
inline void legIK(int leg, const float target[3], float q[3]) {
    const LegGeometry &g = LEG_GEOMETRY[leg];
    const LegConfig &c = legConfig(leg);

    const float px = target[0] - g.hip[0];
    const float ay = target[1] - g.hip[1];
    const float pz = target[2] - g.hip[2];
    const float r2 = px * px + pz * pz;
    float az2 = r2 - c.x_off * c.x_off;
    if (az2 < 0.f) az2 = 0.f;
    const float az = -std::sqrt(az2);

    const float denom = r2 > 1e-12f ? r2 : 1e-12f;
    const float cos_phi = (c.x_off * px + az * pz) / denom;
    const float sin_phi = (az * px - c.x_off * pz) / denom;
    q[0] = std::atan2(sin_phi, cos_phi) / g.sy;

    const float ty = ay - c.o0[0];
    const float tz = az - c.o0[1];
    float d = std::hypot(ty, tz);
    if (d > c.L1 + c.L2 - 1e-6f) d = c.L1 + c.L2 - 1e-6f;
    float cos_psi = (d * d - c.L1 * c.L1 - c.L2 * c.L2) / (2.f * c.L1 * c.L2);
    if (cos_psi > 1.f) cos_psi = 1.f;
    if (cos_psi < -1.f) cos_psi = -1.f;
    const float psi = -std::acos(cos_psi);
    const float alpha = std::atan2(tz, ty) - std::atan2(c.L2 * std::sin(psi), c.L1 + c.L2 * std::cos(psi)) - c.a1;
    const float beta = psi - (c.a2 - c.a1);
    q[1] = alpha / g.sf;
    q[2] = beta / g.st;
}

// Angle wrapped into [-pi, pi).
inline float wrap(float a) {
    a = std::fmod(a + (float)M_PI, 2.f * (float)M_PI);
    if (a < 0.f) a += 2.f * (float)M_PI;
    return a - (float)M_PI;
}

} // namespace spot_pico
