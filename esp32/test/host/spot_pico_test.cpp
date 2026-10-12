// Host test of the spot_pico kinematics and baseline gait against the Python the residual policy is trained on, built
// and run by test_host_programs.py. spot_pico_golden.h is written by simulation/export_golden.py; a failure here means
// WALK_NN would run a different gait or IK than the simulation, which silently invalidates any exported policy.
#include <cmath>
#include <cstdio>
#include <spot_pico/kinematics.h>
#include <spot_pico/residual_gait.h>

#include "spot_pico_golden.h"

using namespace spot_pico;

static int failures = 0;

static void within(float tolerance, float error, const char *what) {
    if (error <= tolerance) return;
    std::printf("FAIL %s: max error %.3e > %.1e\n", what, error, tolerance);
    failures++;
}

static float maxAbsDiff(const float *a, const float *b, int n) {
    float worst = 0.f;
    for (int i = 0; i < n; ++i) worst = std::fmax(worst, std::fabs(a[i] - b[i]));
    return worst;
}

static GaitCoef goldenCoef() {
    const float *c = golden::GAIT_COEF;
    return {c[0], c[1], c[2], c[3], c[4], c[5], c[6]};
}

static void defaultFeetMatchTheSimulation() {
    float feet[LEG_COUNT * 3];
    for (int leg = 0; leg < LEG_COUNT; ++leg) defaultFoot(leg, feet + leg * 3);
    within(1e-7f, maxAbsDiff(feet, golden::DEFAULT_FEET, LEG_COUNT * 3), "default feet (m)");
}

static void standPoseMatchesTheSimulation() {
    float q[LEG_COUNT * 3];
    for (int leg = 0; leg < LEG_COUNT; ++leg) {
        float foot[3];
        defaultFoot(leg, foot);
        legIK(leg, foot, q + leg * 3);
    }
    within(1e-5f, maxAbsDiff(q, golden::STAND_POSE, LEG_COUNT * 3), "stand pose (rad)");
}

// The targets spread up to 30 mm around each stance foot, so the workspace clamp is covered too.
static void legIkMatchesTheSimulation() {
    float worst = 0.f;
    for (int i = 0; i < golden::IK_CASES; ++i) {
        float q[3];
        legIK(golden::IK_LEG[i], golden::IK_TARGET + i * 3, q);
        worst = std::fmax(worst, maxAbsDiff(q, golden::IK_ANGLES + i * 3, 3));
    }
    within(1e-4f, worst, "leg IK (rad)");
}

// Replays QuadrupedMjEnv._action_to_joints with a zero residual for every golden command.
static void baselineGaitMatchesTheSimulation() {
    const GaitCoef coef = goldenCoef();
    float worstPhase = 0.f, worstFeet = 0.f, worstJoints = 0.f;
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

            worstPhase = std::fmax(worstPhase, std::fabs(phase - golden::GAIT_PHASE[sample]));
            worstFeet = std::fmax(worstFeet, maxAbsDiff(&feet[0][0], golden::GAIT_FEET + sample * 12, 12));
            worstJoints = std::fmax(worstJoints, maxAbsDiff(q, golden::GAIT_JOINTS + sample * 12, 12));
            ++sample;
        }
    }
    if (sample != golden::GAIT_COMMANDS * golden::GAIT_SAMPLES) {
        std::printf("FAIL gait: compared %d samples, the golden holds %d\n", sample,
                    golden::GAIT_COMMANDS * golden::GAIT_SAMPLES);
        failures++;
    }
    within(1e-5f, worstPhase, "gait phase");
    within(1e-6f, worstFeet, "gait feet (m)");
    within(1e-4f, worstJoints, "gait joints (rad)");
}

int main() {
    defaultFeetMatchTheSimulation();
    standPoseMatchesTheSimulation();
    legIkMatchesTheSimulation();
    baselineGaitMatchesTheSimulation();
    if (failures) std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
