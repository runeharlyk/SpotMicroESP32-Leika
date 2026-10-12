// Host test of WALK_NN, built and run by test_host_programs.py with the synthetic policy in fixtures/ (written by
// simulation/export_golden.py), so the state is compiled as in a firmware that carries a trained one. walk_nn_golden.h
// replays the real QuadrupedMjEnv.step with that network; the state must reproduce it tick for tick.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <motion.h>
#include <peripherals/servo_output.h>
#include <variant.h>

#include "walk_nn_golden.h"

static_assert(WALK_NN_POLICY, "the fixture policy must be on the include path");

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void within(float tolerance, float error, const char *what) {
    if (error <= tolerance) return;
    std::printf("FAIL %s: max error %.3e > %.1e\n", what, error, tolerance);
    failures++;
}

constexpr float DT = leika_policy::CONTROL_DT;
constexpr int SETTLE_TICKS = static_cast<int>(WalkNNState::SETTLE_S / DT) + 2;

static ImuSample imuSample(const float *quat, const float *gyro) {
    ImuSample imu;
    imu.quat = {quat[0], quat[1], quat[2], quat[3]};
    imu.gyro = {gyro[0], gyro[1], gyro[2]};
    imu.valid = ImuValid::ACCEL | ImuValid::GYRO | ImuValid::ORIENTATION;
    return imu;
}

static ImuSample levelImu() {
    const float quat[4] = {1, 0, 0, 0}, gyro[3] = {0, 0, 0};
    return imuSample(quat, gyro);
}

static CommandMsg goldenJoystick() {
    CommandMsg command {};
    command.ly = golden_walk_nn::JOYSTICK[0];
    command.lx = golden_walk_nn::JOYSTICK[1];
    command.rx = golden_walk_nn::JOYSTICK[2];
    return command;
}

struct Rig {
    WalkNNState state;
    body_state_t body;
    float degrees[12] = {};

    explicit Rig(const ImuSample &imu = levelImu()) {
        state.configure(KIN_CONFIG_SPOTMICRO_ESP32_MINI);
        state.begin();
        state.observe(imu);
    }

    void tick() {
        state.step(body, DT);
        state.jointAngles(degrees);
    }

    bool settle() {
        for (int i = 0; i < SETTLE_TICKS && !state.engaged(); ++i) tick();
        return state.engaged();
    }
};

static void theFixturePolicyPassesItsSelfCheck() { CHECK(WalkNNState::verifyPolicy()); }

static void policyTicksMatchTheSimulation() {
    Rig rig;
    rig.state.handleCommand(goldenJoystick());
    CHECK(rig.settle());
    float worstFeet = 0, worstDegrees = 0;
    for (int t = 0; t < golden_walk_nn::TICKS; ++t) {
        if (t > 0) rig.tick();
        float expected[12];
        spot_pico::firmwareAngles(golden_walk_nn::JOINTS + t * 12, expected);
        for (int i = 0; i < 12; i++) worstDegrees = std::fmax(worstDegrees, std::fabs(rig.degrees[i] - expected[i]));
        for (int fw = 0; fw < 4; ++fw) {
            const int leg = spot_pico::FIRMWARE_TO_SIM_LEG[fw];
            float home[3];
            spot_pico::defaultFoot(leg, home);
            const float *standing = KIN_CONFIG_SPOTMICRO_ESP32_MINI.default_feet_positions[fw];
            const float foot[3] = {home[0] + rig.body.feet[fw][2] - standing[2],
                                   home[1] - (rig.body.feet[fw][0] - standing[0]),
                                   home[2] + rig.body.feet[fw][1] - standing[1]};
            for (int k = 0; k < 3; ++k)
                worstFeet = std::fmax(worstFeet, std::fabs(foot[k] - golden_walk_nn::FEET[t * 12 + leg * 3 + k]));
        }
    }
    within(1e-5f, worstFeet, "policy feet (m)");
    // float32 IK against the float64 env: 0.03 degrees is a sixteenth of one PCA9685 count.
    within(0.03f, worstDegrees, "policy joints (deg)");
}

static void theObservationMatchesTheEnv() {
    Rig rig(imuSample(golden_walk_nn::IMU_QUAT, golden_walk_nn::IMU_GYRO));
    CHECK(rig.settle());
    float worst = 0;
    for (int c = 0; c < golden_walk_nn::OBSERVATION_CASES; ++c) {
        rig.state.observe(imuSample(golden_walk_nn::IMU_QUAT + c * 4, golden_walk_nn::IMU_GYRO + c * 3));
        float obs[leika_policy::OBS_DIM];
        rig.state.observation(obs);
        for (int i = 0; i < 9; i++)
            worst = std::fmax(worst, std::fabs(obs[i] - golden_walk_nn::OBSERVATION[c * 9 + i]));
    }
    within(1e-5f, worst, "IMU observation");
}

// The joints ease from their last commands to the trained stance, never faster than the servos turn, and only then
// does the policy take over.
static void entryEasesIntoTheStanceBeforeThePolicyEngages() {
    Rig rig;
    float from[12];
    for (int i = 0; i < 12; i++) from[i] = rig.degrees[i] = 40.0f * (i % 3) - 15.0f;
    rig.tick();
    float firstStep = 0;
    for (int i = 0; i < 12; i++) firstStep = std::fmax(firstStep, std::fabs(rig.degrees[i] - from[i]));
    CHECK(firstStep < 0.01f);
    CHECK(!rig.state.engaged());

    int ticks = 1;
    while (!rig.state.engaged() && ticks < SETTLE_TICKS) {
        float before[12];
        std::memcpy(before, rig.degrees, sizeof(before));
        rig.tick();
        ticks++;
        if (!rig.state.engaged()) {
            float tickStep = 0;
            for (int i = 0; i < 12; i++) tickStep = std::fmax(tickStep, std::fabs(rig.degrees[i] - before[i]));
            CHECK(tickStep < SERVO_MAX_SPEED_DEG_S * DT);
        }
    }
    CHECK(rig.state.engaged());
    CHECK(ticks * DT >= WalkNNState::SETTLE_S);
}

static void tick(MotionService &motion, const ImuSample &imu) {
    fake_timer::nowUs += static_cast<int64_t>(DT * 1e6f);
    motion.update(imu, eGestureNone);
}

static void startMini(MotionService &motion) {
    motion.useConfig(kinConfigFor(socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI));
    motion.begin();
}

static void walkNNNeedsThePolicyTheMiniAndTheImu() {
    MotionService motion;
    motion.useConfig(kinConfigFor(socket_message_KinematicsVariant_SPOTMICRO_ESP32));
    motion.begin();
    CHECK(motion.walkNNAvailable());
    motion.inbox.postMode(socket_message_ModesEnum_WALK_NN);
    tick(motion, levelImu());
    CHECK(motion.mode() == socket_message_ModesEnum_DEACTIVATED);
    CHECK(!motion.takeModeApplied());

    startMini(motion);
    ImuSample blind = levelImu();
    blind.valid = ImuValid::ACCEL;
    motion.inbox.postMode(socket_message_ModesEnum_WALK_NN);
    tick(motion, blind);
    CHECK(motion.mode() == socket_message_ModesEnum_DEACTIVATED);

    motion.inbox.postMode(socket_message_ModesEnum_WALK_NN);
    tick(motion, levelImu());
    CHECK(motion.mode() == socket_message_ModesEnum_WALK_NN);
    CHECK(motion.isActive());
    CHECK(motion.takeModeApplied());

    motion.inbox.postMode(socket_message_ModesEnum_DEACTIVATED);
    tick(motion, levelImu());
    CHECK(!motion.isActive());
}

// A robot that was being driven when the link fell silent must end up where one that never was driven does.
static void aLostLinkStopsTheLearnedGait() {
    MotionService driven, idle;
    startMini(driven);
    startMini(idle);
    for (MotionService *motion : {&driven, &idle}) {
        motion->inbox.postMode(socket_message_ModesEnum_WALK_NN);
        tick(*motion, levelImu());
    }
    socket_message_ControllerData stick = socket_message_ControllerData_init_zero;
    stick.has_left = true;
    stick.left.y = 0.8f;
    driven.inbox.postInput(stick, fake_timer::nowUs / 1000, fake_timer::nowUs);
    for (int t = 0; t < SETTLE_TICKS + 40; t++) {
        fake_timer::nowUs += static_cast<int64_t>(DT * 1e6f);
        driven.update(levelImu(), eGestureNone);
        idle.update(levelImu(), eGestureNone);
    }
    CHECK(driven.linkLost());
    float worst = 0;
    for (int i = 0; i < 12; i++) worst = std::fmax(worst, std::fabs(driven.getAngles()[i] - idle.getAngles()[i]));
    within(1e-4f, worst, "driven then lost against never driven (deg)");
}

// The joint map and the generic Mini IK describe the same robot: the trained stance (feet under the hips, the stance
// depth below them) mapped onto the firmware must land near what the firmware's own IK commands for that pose. The
// two geometries place the feet differently sideways, which leaves up to 17 degrees at the hips; a swapped leg or a
// mirrored femur or knee is over a hundred off.
static void theTrainedStanceMapsOntoTheFirmwaresOwnStance() {
    MotionService motion;
    startMini(motion);
    motion.inbox.postMode(socket_message_ModesEnum_WALK_NN);
    for (int t = 0; t < SETTLE_TICKS; t++) tick(motion, levelImu());
    float walkNN[12];
    std::memcpy(walkNN, motion.getAngles(), sizeof(walkNN));

    Kinematics kinematics(KIN_CONFIG_SPOTMICRO_ESP32_MINI);
    body_state_t body;
    body.ym = spot_pico::STANCE_DEPTH;
    body.updateFeet(KIN_CONFIG_SPOTMICRO_ESP32_MINI.default_feet_positions);
    float ik[12];
    kinematics.calculate_inverse_kinematics(body, ik);
    float worst = 0;
    for (int i = 0; i < 12; i++) worst = std::fmax(worst, std::fabs(walkNN[i] - ik[i] * MotionService::JOINT_DIRECTION[i]));
    within(20.0f, worst, "trained stance against the generic IK (deg)");
}

int main() {
    theFixturePolicyPassesItsSelfCheck();
    policyTicksMatchTheSimulation();
    theObservationMatchesTheEnv();
    entryEasesIntoTheStanceBeforeThePolicyEngages();
    walkNNNeedsThePolicyTheMiniAndTheImu();
    aLostLinkStopsTheLearnedGait();
    theTrainedStanceMapsOntoTheFirmwaresOwnStance();
    if (failures) std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
