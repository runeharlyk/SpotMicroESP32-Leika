// Parity between the firmware's spot_pico headers and the Python the residual policy is trained on.
// golden.h is written by simulation/export_golden.py; a failure here means the robot would run a
// different gait or IK than the simulation, which silently invalidates any exported policy.

#include <unity.h>

#include <cmath>
#include <cstdio>
#include <spot_pico/kinematics.h>
#include <spot_pico/residual_gait.h>

#include "golden.h"

using namespace spot_pico;

namespace {

float maxAbsDiff(const float *a, const float *b, int n) {
    float worst = 0.f;
    for (int i = 0; i < n; ++i) worst = std::fmax(worst, std::fabs(a[i] - b[i]));
    return worst;
}

void assertWithin(float tolerance, float error, const char *what) {
    char msg[96];
    std::snprintf(msg, sizeof(msg), "%s max error %.3e > %.1e", what, error, tolerance);
    TEST_ASSERT_TRUE_MESSAGE(error <= tolerance, msg);
}

GaitCoef goldenCoef() {
    const float *c = golden::GAIT_COEF;
    return {c[0], c[1], c[2], c[3], c[4], c[5], c[6]};
}

} // namespace

void setUp() {}
void tearDown() {}

void test_default_feet_match_the_simulation() {
    float feet[LEG_COUNT * 3];
    for (int leg = 0; leg < LEG_COUNT; ++leg) defaultFoot(leg, feet + leg * 3);
    assertWithin(1e-7f, maxAbsDiff(feet, golden::DEFAULT_FEET, LEG_COUNT * 3), "default feet (m)");
}

void test_stand_pose_matches_the_simulation() {
    float q[LEG_COUNT * 3];
    for (int leg = 0; leg < LEG_COUNT; ++leg) {
        float foot[3];
        defaultFoot(leg, foot);
        legIK(leg, foot, q + leg * 3);
    }
    assertWithin(1e-5f, maxAbsDiff(q, golden::STAND_POSE, LEG_COUNT * 3), "stand pose (rad)");
}

void test_leg_ik_matches_the_simulation() {
    float worst = 0.f;
    for (int i = 0; i < golden::IK_CASES; ++i) {
        float q[3];
        legIK(golden::IK_LEG[i], golden::IK_TARGET + i * 3, q);
        worst = std::fmax(worst, maxAbsDiff(q, golden::IK_ANGLES + i * 3, 3));
    }
    assertWithin(1e-4f, worst, "leg IK (rad)");
}

void test_leg_fk_matches_the_simulation() {
    float worst = 0.f;
    for (int i = 0; i < golden::FK_CASES; ++i) {
        float foot[3];
        legFK(golden::FK_LEG[i], golden::FK_Q + i * 3, foot);
        worst = std::fmax(worst, maxAbsDiff(foot, golden::FK_FOOT + i * 3, 3));
    }
    assertWithin(1e-6f, worst, "leg FK (m)");
}

// IK must place the foot where it was asked; only the in-reach golden targets (spread <= 12 mm) count.
void test_ik_then_fk_returns_the_target() {
    float worst = 0.f;
    for (int i = 0; i < golden::IK_CASES; ++i) {
        if (i % 6 == 5) continue;  // the 30 mm spread case may be clamped at the workspace edge
        float q[3], foot[3];
        legIK(golden::IK_LEG[i], golden::IK_TARGET + i * 3, q);
        legFK(golden::IK_LEG[i], q, foot);
        worst = std::fmax(worst, maxAbsDiff(foot, golden::IK_TARGET + i * 3, 3));
    }
    assertWithin(2e-6f, worst, "IK->FK round trip (m)");
}

void test_tibia_linkage_matches_the_simulation() {
    float worst = 0.f;
    for (int i = 0; i < golden::LINKAGE_CASES; ++i) {
        const float *row = golden::LINKAGE_TIBIA_FEMUR_SERVO + i * 3;
        float servo;
        TEST_ASSERT_TRUE(linkage::servoFromTibia(row[0], row[1], servo));
        worst = std::fmax(worst, std::fabs(servo - row[2]));
    }
    assertWithin(1e-5f, worst, "servo from tibia (rad)");
}

void test_tibia_linkage_round_trips() {
    float worst = 0.f;
    for (int i = 0; i < golden::LINKAGE_CASES; ++i) {
        const float *row = golden::LINKAGE_TIBIA_FEMUR_SERVO + i * 3;
        float servo, tibia;
        TEST_ASSERT_TRUE(linkage::servoFromTibia(row[0], row[1], servo));
        TEST_ASSERT_TRUE(linkage::tibiaFromServo(servo, row[1], tibia));
        worst = std::fmax(worst, std::fabs(tibia - row[0]));
    }
    assertWithin(1e-5f, worst, "tibia->servo->tibia (rad)");
}

// Replays QuadrupedMjEnv._action_to_joints with a zero residual for every golden command.
void test_baseline_gait_matches_the_simulation() {
    const GaitCoef coef = goldenCoef();
    float worst_phase = 0.f, worst_feet = 0.f, worst_joints = 0.f;
    int sample = 0;
    for (int c = 0; c < golden::GAIT_COMMANDS; ++c) {
        GaitParams gait;
        float phase = 0.f;
        for (int step = 0; step < golden::GAIT_STEPS; ++step) {
            analyticGait(golden::GAIT_CMD + c * 3, coef, gait);
            phase = advancePhase(phase, gait, golden::CONTROL_DT);
            if (step % golden::GAIT_SAMPLE_EVERY != golden::GAIT_SAMPLE_EVERY - 1) continue;

            float feet[LEG_COUNT][3], q[LEG_COUNT * 3];
            generateFeet(gait, phase, feet);
            for (int leg = 0; leg < LEG_COUNT; ++leg) legIK(leg, feet[leg], q + leg * 3);

            worst_phase = std::fmax(worst_phase, std::fabs(phase - golden::GAIT_PHASE[sample]));
            worst_feet = std::fmax(worst_feet, maxAbsDiff(&feet[0][0], golden::GAIT_FEET + sample * 12, 12));
            worst_joints = std::fmax(worst_joints, maxAbsDiff(q, golden::GAIT_JOINTS + sample * 12, 12));
            ++sample;
        }
    }
    TEST_ASSERT_EQUAL_INT(golden::GAIT_COMMANDS * golden::GAIT_SAMPLES, sample);
    assertWithin(1e-5f, worst_phase, "gait phase");
    assertWithin(1e-6f, worst_feet, "gait feet (m)");
    assertWithin(1e-4f, worst_joints, "gait joints (rad)");
}

// A zero command must hold the default stance; the gait only moves feet when commanded.
void test_zero_command_holds_the_stance() {
    GaitParams gait;
    const float zero[3] = {0, 0, 0};
    analyticGait(zero, goldenCoef(), gait);
    float feet[LEG_COUNT][3];
    generateFeet(gait, 0.37f, feet);
    for (int leg = 0; leg < LEG_COUNT; ++leg) {
        float home[3];
        defaultFoot(leg, home);
        assertWithin(1e-7f, maxAbsDiff(feet[leg], home, 3), "idle foot drift (m)");
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_default_feet_match_the_simulation);
    RUN_TEST(test_stand_pose_matches_the_simulation);
    RUN_TEST(test_leg_ik_matches_the_simulation);
    RUN_TEST(test_leg_fk_matches_the_simulation);
    RUN_TEST(test_ik_then_fk_returns_the_target);
    RUN_TEST(test_tibia_linkage_matches_the_simulation);
    RUN_TEST(test_tibia_linkage_round_trips);
    RUN_TEST(test_baseline_gait_matches_the_simulation);
    RUN_TEST(test_zero_command_holds_the_stance);
    return UNITY_END();
}
