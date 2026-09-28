// WalkNNState must reproduce QuadrupedMjEnv.step tick for tick. golden_walk_nn.h replays the real env
// with the fixture policy (esp32/test/fixtures) and a level, motionless IMU; this test drives the
// firmware pipeline the same way MotionService does: observe -> step -> inverse kinematics.

#define SPOTMICRO_ESP32_MINI
#include <unity.h>

#include <cmath>
#include <cstdio>
#include <kinematics.h>
#include <motion_states/walk_nn_state.h>

#include "golden_walk_nn.h"

namespace {

// World-from-sensor quaternion that makes the base level: R_world_base = S * IMU_FROM_BASE = I.
void levelQuaternion(float q[4]) {
    const auto &M = WalkNNState::IMU_FROM_BASE;
    const float S[3][3] = {{M[0][0], M[1][0], M[2][0]}, {M[0][1], M[1][1], M[2][1]}, {M[0][2], M[1][2], M[2][2]}};
    q[0] = 0.5f * std::sqrt(std::fmax(0.f, 1.f + S[0][0] + S[1][1] + S[2][2]));
    q[1] = std::copysign(0.5f * std::sqrt(std::fmax(0.f, 1.f + S[0][0] - S[1][1] - S[2][2])), S[2][1] - S[1][2]);
    q[2] = std::copysign(0.5f * std::sqrt(std::fmax(0.f, 1.f - S[0][0] + S[1][1] - S[2][2])), S[0][2] - S[2][0]);
    q[3] = std::copysign(0.5f * std::sqrt(std::fmax(0.f, 1.f - S[0][0] - S[1][1] + S[2][2])), S[1][0] - S[0][1]);
}

struct Rig {
    WalkNNState state;
    Kinematics kinematics;
    body_state_t body;
    float quat[4];
    float gyro[3] = {0, 0, 0};
    float degrees[12];

    Rig() {
        levelQuaternion(quat);
        body.updateFeet(KinConfig::default_feet_positions);
        kinematics.calculate_inverse_kinematics(body, degrees);
        state.begin();
        CommandMsg cmd {};
        cmd.ly = golden_walk_nn::JOYSTICK[0];
        cmd.lx = golden_walk_nn::JOYSTICK[1];
        cmd.rx = golden_walk_nn::JOYSTICK[2];
        state.handleCommand(cmd);
    }

    void tick() {
        state.observe(quat, gyro, kinematics.jointRadians());
        state.step(body, leika_policy::CONTROL_DT);
        kinematics.calculate_inverse_kinematics(body, degrees);
    }

    bool settle(int max_ticks) {
        for (int i = 0; i < max_ticks && !state.engaged(); ++i) tick();
        return state.engaged();
    }
};

} // namespace

void setUp() {}
void tearDown() {}

void test_settles_into_the_trained_stance_before_engaging() {
    Rig rig;
    TEST_ASSERT_FALSE(rig.state.engaged());
    TEST_ASSERT_TRUE_MESSAGE(rig.settle(2000), "never reached the stance");
    rig.tick();  // first policy tick
    TEST_ASSERT_TRUE(rig.state.engaged());
}

void test_policy_ticks_match_the_simulation() {
    Rig rig;
    TEST_ASSERT_TRUE(rig.settle(2000));
    float worst_feet = 0.f, worst_joints = 0.f;
    int first_bad_tick = -1;
    for (int t = 0; t < golden_walk_nn::TICKS; ++t) {
        rig.tick();
        const float *expected_feet = golden_walk_nn::FEET + t * 12;
        const float *expected_joints = golden_walk_nn::JOINTS + t * 12;
        float tick_error = 0.f;
        for (int fw = 0; fw < 4; ++fw) {
            const int leg = spot_pico::FIRMWARE_TO_SIM_LEG[fw];
            const float sim[3] = {rig.body.feet[fw][2], -rig.body.feet[fw][0], rig.body.feet[fw][1] - rig.body.ym};
            for (int k = 0; k < 3; ++k) tick_error = std::fmax(tick_error, std::fabs(sim[k] - expected_feet[leg * 3 + k]));
        }
        worst_feet = std::fmax(worst_feet, tick_error);
        for (int i = 0; i < 12; ++i) {
            worst_joints = std::fmax(worst_joints, std::fabs(rig.kinematics.jointRadians()[i] - expected_joints[i]));
        }
        if (first_bad_tick < 0 && tick_error > 1e-5f) first_bad_tick = t;
    }
    char msg[128];
    std::snprintf(msg, sizeof(msg), "feet max error %.3e m (first bad tick %d), joints %.3e rad", worst_feet,
                  first_bad_tick, worst_joints);
    // float32 IK vs the float64 env: 5e-4 rad is ~17x below one PCA9685 count (~0.5 deg at 50 Hz).
    TEST_ASSERT_TRUE_MESSAGE(worst_feet < 1e-5f && worst_joints < 5e-4f, msg);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_settles_into_the_trained_stance_before_engaging);
    RUN_TEST(test_policy_ticks_match_the_simulation);
    return UNITY_END();
}
