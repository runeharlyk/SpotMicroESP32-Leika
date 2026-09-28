#pragma once

// Leika Mini (spot_pico) leg kinematics: C++ port of simulation/src/robot/firmware_gait.py
// (leg_fk, leg_ik, SpotPicoKinConfig, DEFAULT_FEET) and simulation/src/resources/spot_pico/linkage.py.
// Keep both sides in lockstep; esp32/test/test_spot_pico checks this file against golden vectors
// generated from the Python (simulation/export_golden.py).
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

// Forward kinematics: foot = H + Ry(sy*q1) (p_femur + Rx(sf*q2) (p_tibia + Rx(st*q3) p_foot)).
inline void legFK(int leg, const float q[3], float foot[3]) {
    const LegGeometry &g = LEG_GEOMETRY[leg];
    const float a3 = g.st * q[2], a2 = g.sf * q[1], a1 = g.sy * q[0];

    const float inner[3] = {g.p_tibia[0] + g.p_foot[0],
                            g.p_tibia[1] + std::cos(a3) * g.p_foot[1] - std::sin(a3) * g.p_foot[2],
                            g.p_tibia[2] + std::sin(a3) * g.p_foot[1] + std::cos(a3) * g.p_foot[2]};
    const float arm[3] = {g.p_femur[0] + inner[0], g.p_femur[1] + std::cos(a2) * inner[1] - std::sin(a2) * inner[2],
                          g.p_femur[2] + std::sin(a2) * inner[1] + std::cos(a2) * inner[2]};
    foot[0] = g.hip[0] + std::cos(a1) * arm[0] + std::sin(a1) * arm[2];
    foot[1] = g.hip[1] + arm[1];
    foot[2] = g.hip[2] - std::sin(a1) * arm[0] + std::cos(a1) * arm[2];
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

// Tibia four-bar linkage (linkage.py): the tibia servo drives a crank coaxial with the femur joint,
// so the servo horn angle is not the tibia joint angle. Lengths in cm, angles in radians.
namespace linkage {

constexpr float R1 = 2.2222f, ROD = 5.0125f, R2 = 1.3006f, G = 5.7925f;
constexpr float PHI0 = 65.31f * (float)M_PI / 180.f;  // crank angle at stance, from the femur line
constexpr float PSI0 = 85.68f * (float)M_PI / 180.f;  // lever angle at stance, from the femur line

inline float wrap(float a) {
    a = std::fmod(a + (float)M_PI, 2.f * (float)M_PI);
    if (a < 0.f) a += 2.f * (float)M_PI;
    return a - (float)M_PI;
}

// Picks the four-bar closure branch nearest the stance angle; returns false if the linkage cannot close.
inline bool closeLinkage(float bx, float bz, float arm, float stance, float &out) {
    const float dist = std::hypot(bx, bz);
    const float c = (dist * dist + arm * arm - ROD * ROD) / (2.f * dist * arm);
    if (std::fabs(c) > 1.f) return false;
    const float a = std::atan2(bz, bx);
    const float c1 = a + std::acos(c), c2 = a - std::acos(c);
    out = std::fabs(wrap(c1 - stance)) <= std::fabs(wrap(c2 - stance)) ? c1 : c2;
    return true;
}

// Servo horn angle (relative to the coxa, 0 = stance) needed for a tibia joint angle.
inline bool servoFromTibia(float q_tibia, float q_femur, float &q_servo) {
    const float psi = PSI0 + q_tibia;
    float phi;
    if (!closeLinkage(G + R2 * std::cos(psi), R2 * std::sin(psi), R1, PHI0, phi)) return false;
    q_servo = wrap(phi - PHI0) + q_femur;
    return true;
}

// Tibia joint angle produced by a servo horn angle.
inline bool tibiaFromServo(float q_servo, float q_femur, float &q_tibia) {
    const float phi = PHI0 + (q_servo - q_femur);
    float psi;
    if (!closeLinkage(R1 * std::cos(phi) - G, R1 * std::sin(phi), R2, PSI0, psi)) return false;
    q_tibia = wrap(psi - PSI0);
    return true;
}

} // namespace linkage

} // namespace spot_pico
