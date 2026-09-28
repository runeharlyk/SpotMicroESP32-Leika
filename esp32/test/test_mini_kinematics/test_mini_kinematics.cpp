// The Mini runs the firmware's generic body/feet pipeline (x forward, y up, z left; legs FL, FR, RL, RR)
// on spot_pico IK (+X left, +Y rear, +Z up; legs fr, fl, rr, rl). These tests pin that mapping: a
// swapped leg or axis would still produce plausible angles, just for the wrong foot.

#define SPOTMICRO_ESP32_MINI
#include <unity.h>

#include <cmath>
#include <kinematics.h>

#include "../test_spot_pico/golden.h"

namespace {

// Body height that puts the firmware's default feet exactly on the spot_pico stance.
constexpr float STANCE_BODY_HEIGHT = -spot_pico::stanceFoot(0, 2);

body_state_t stanceBody() {
    body_state_t body;
    body.ym = STANCE_BODY_HEIGHT;
    body.updateFeet(KinConfig::default_feet_positions);
    return body;
}

float maxAbsDiff(const float *a, const float *b, int n) {
    float worst = 0.f;
    for (int i = 0; i < n; ++i) worst = std::fmax(worst, std::fabs(a[i] - b[i]));
    return worst;
}

} // namespace

void setUp() {}
void tearDown() {}

void test_default_stance_reproduces_the_simulated_stand_pose() {
    Kinematics kin;
    float degrees[12];
    TEST_ASSERT_EQUAL_INT(ESP_OK, kin.calculate_inverse_kinematics(stanceBody(), degrees));
    TEST_ASSERT_TRUE(maxAbsDiff(kin.jointRadians(), golden::STAND_POSE, 12) < 1e-5f);
}

// Leg 0 is front-left: its spot_pico target must be on the +X (left) side and toward -Y (front).
void test_firmware_legs_land_on_the_matching_corners() {
    Kinematics kin;
    float degrees[12];
    kin.calculate_inverse_kinematics(stanceBody(), degrees);
    const bool left[4] = {true, false, true, false};
    const bool front[4] = {true, true, false, false};
    for (int fw = 0; fw < 4; ++fw) {
        const int leg = spot_pico::FIRMWARE_TO_SIM_LEG[fw];
        float foot[3];
        spot_pico::legFK(leg, kin.jointRadians() + leg * 3, foot);
        TEST_ASSERT_EQUAL_MESSAGE(left[fw], foot[0] > 0.f, "left/right side");
        TEST_ASSERT_EQUAL_MESSAGE(front[fw], foot[1] < 0.f, "front/rear side");
    }
}

// Shifting the body forward and up must move every foot backward and down relative to the body.
void test_body_translation_moves_the_feet_the_opposite_way() {
    Kinematics kin;
    float degrees[12];
    kin.calculate_inverse_kinematics(stanceBody(), degrees);
    float before[4][3];
    for (int leg = 0; leg < 4; ++leg) spot_pico::legFK(leg, kin.jointRadians() + leg * 3, before[leg]);

    body_state_t shifted = stanceBody();
    shifted.xm = 0.006f;
    shifted.ym += 0.004f;
    kin.calculate_inverse_kinematics(shifted, degrees);
    for (int leg = 0; leg < 4; ++leg) {
        float after[3];
        spot_pico::legFK(leg, kin.jointRadians() + leg * 3, after);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.006f, after[1] - before[leg][1]);   // rearward = +Y
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, -0.004f, after[2] - before[leg][2]);  // downward = -Z
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.f, after[0] - before[leg][0]);
    }
}

// Servo degrees: hip and femur follow their joints, the tibia goes through the four-bar linkage.
void test_servo_degrees_follow_the_joints_and_linkage() {
    Kinematics kin;
    float degrees[12];
    kin.calculate_inverse_kinematics(stanceBody(), degrees);
    for (int fw = 0; fw < 4; ++fw) {
        const float *q = kin.jointRadians() + spot_pico::FIRMWARE_TO_SIM_LEG[fw] * 3;
        float servo;
        TEST_ASSERT_TRUE(spot_pico::linkage::servoFromTibia(q[2], q[1], servo));
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, RAD_TO_DEG_F(q[0]), degrees[fw * 3]);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, RAD_TO_DEG_F(q[1]), degrees[fw * 3 + 1]);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, RAD_TO_DEG_F(servo), degrees[fw * 3 + 2]);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_default_stance_reproduces_the_simulated_stand_pose);
    RUN_TEST(test_firmware_legs_land_on_the_matching_corners);
    RUN_TEST(test_body_translation_moves_the_feet_the_opposite_way);
    RUN_TEST(test_servo_degrees_follow_the_joints_and_linkage);
    return UNITY_END();
}
