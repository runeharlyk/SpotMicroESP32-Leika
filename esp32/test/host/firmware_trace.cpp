// Replays a fixed control script through the firmware's own motion code and prints the result as
// JSON, so the web app's TypeScript port (app/src/lib/simulation/firmware) can be pinned to it, and the
// simulation's port of the odometry (simulation/src/robot/firmware_odometry.py) too: the kinematic one, and the fused
// one fed a made-up gyro that reads more turn than the feet, with a bias and a wobble, and none in the crawl.
// Run once per kinematics variant by export_firmware_traces.py, which passes the variant's name.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <odometry.h>
#include <variant.h>
#include <motion_states/rest_state.h>
#include <motion_states/stand_state.h>
#include <motion_states/walk_state.h>

namespace {

enum Mode { REST = 3, STAND = 4, WALK = 5 };
enum Gait { TROT = 0, CRAWL = 1, AUTO = 2 };

struct Segment {
    Mode mode;
    Gait gait;
    CommandMsg cmd;
    float imu_x, imu_y;
    int ticks;
};

constexpr float DT = 0.01f;

// lx, ly, rx, ry, h, s, s1
const Segment SCRIPT[] = {
    {REST, TROT, {0, 0, 0, 0, 0, 0, 0}, 0, 0, 50},
    {STAND, TROT, {0.3f, -0.2f, 0.4f, -0.3f, 0.6f, 0, 0}, 0.02f, -0.03f, 100},
    // Level again before walking, as a client does, then each walk long enough for whole cycles once the commands
    // settle, as the joint speed cap slows them.
    {STAND, TROT, {0, 0, 0, 0, 0.5f, 0, 0}, 0, 0, 100},
    {WALK, TROT, {0, 1, 0, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 300},
    {WALK, TROT, {-1, 0, 0, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 300},
    {WALK, TROT, {0, 0, 1, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 300},
    // Both sticks to the rim on a raised body, where the legs reach only a fraction of the stroke.
    {WALK, TROT, {1, 1, 1, 0, 1, 0.5f, 0.5f}, 0, 0, 300},
    {WALK, TROT, {0, 0, 0, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 300},
    {WALK, CRAWL, {0, 0.6f, 0, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 600},
    {STAND, CRAWL, {0, 0, 0, 0, 0.5f, 0, 0}, 0, 0, 50},
    // Out of a state and back while the body is still moving, so a state re-entered with stale smoothing shows.
    {STAND, CRAWL, {0, 0, 0, 0, 1, 0, 0}, 0, 0, 10},
    {WALK, CRAWL, {0, 0, 0, 0, 0, 0.5f, 0.5f}, 0, 0, 5},
    {STAND, CRAWL, {0, 0, 0, 0, 0, 0, 0}, 0, 0, 40},
    // AUTO: a short stick starts the crawl once it has settled, a long one mid-walk stops it and walks on in the trot,
    // which centres the body the crawl shifted, and a short one again stops the trot for the crawl.
    {WALK, TROT, {0, 0, 0, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 50},
    {WALK, AUTO, {0, 0.2f, 0, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 400},
    {WALK, AUTO, {0, 1, 0, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 400},
    {WALK, AUTO, {0.2f, 0, 0, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 400},
    // A gait chosen mid-stride, from AUTO's crawl and then from the trot: the walk stops on all four feet, takes it
    // and walks on.
    {WALK, TROT, {0, 1, 0, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 300},
    {WALK, CRAWL, {0, 1, 0, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 800},
};

// MotionService's state and angle handling (esp32/src/motion.cpp), without its timer and peripherals.
struct Motion {
    const KinConfig &config;
    RestState rest;
    StandState stand;
    WalkState walk;
    Kinematics kinematics;
    MotionState *state = nullptr;
    body_state_t body;
    std::array<bool, 4> stance {};
    KinematicOdometry odometry;
    FusedOdometry fused;
    std::optional<float> gyro;
    float new_angles[12] = {0};
    float angles[12] = {0};

    explicit Motion(const KinConfig &config) : config(config), kinematics(config) {
        rest.configure(config);
        stand.configure(config);
        walk.configure(config);
        body.ym = config.default_body_height;
        body.updateFeet(config.default_feet_positions);
    }

    void setMode(Mode mode) {
        if (state) state->end();
        MotionState *states[] = {&rest, &stand, &walk};
        state = states[mode - REST];
        state->resetSmoothing();
        state->begin();
    }

    void setGait(Gait gait) {
        walk.set_auto_gait(gait == AUTO);
        if (gait == TROT)
            walk.select_gait(WALK_GAIT::TROT);
        else if (gait == CRAWL)
            walk.select_gait(WALK_GAIT::CRAWL);
    }

    void update(float imu_x, float imu_y, bool crawling, int step) {
        ImuSample imu;
        imu.rpy = {imu_x, imu_y, 0};
        state->updateImuOffsets(imu);
        state->step(body, DT);
        stance = state == &walk ? walk.stance() : std::array<bool, 4> {true, true, true, true};
        const float feetBefore = odometry.estimate().yaw;
        odometry.update(body, stance);
        fused.update(body, stance, gyro, DT);
        // The next tick's sample: this tick's turn, a tick late as a gyro's lag.
        const float feetRate = std::remainder(odometry.estimate().yaw - feetBefore, 2 * static_cast<float>(M_PI)) / DT;
        gyro =
            crawling ? std::nullopt : std::optional<float>(1.15f * feetRate + 0.03f + 0.01f * std::sin(0.37f * step));
        kinematics.calculate_inverse_kinematics(body, new_angles);
        for (int i = 0; i < 12; i++) {
            const float angle = new_angles[i] * JOINT_DIRECTION[i];
            if (!isEqual(angle, angles[i], 0.1)) angles[i] = angle;
        }
    }
};

bool sameCommand(const CommandMsg &a, const CommandMsg &b) {
    return a.lx == b.lx && a.ly == b.ly && a.rx == b.rx && a.ry == b.ry && a.h == b.h && a.s == b.s && a.s1 == b.s1;
}

void printFloats(const float *values, int count) {
    for (int i = 0; i < count; i++) printf("%s%.9g", i ? "," : "", values[i]);
}

const KinConfig *configNamed(const char *name) {
    for (int variant = 0; variant <= socket_message_KinematicsVariant_SPOTMICRO_YERTLE; variant++) {
        const auto known = static_cast<KinematicsVariant>(variant);
        if (knownVariant(known) && std::strcmp(variantName(known), name) == 0) return kinConfigFor(known);
    }
    return nullptr;
}

}  // namespace

int main(int argc, char **argv) {
    const KinConfig *config = argc > 1 ? configNamed(argv[1]) : nullptr;
    if (!config) {
        fprintf(stderr, "usage: firmware_trace <variant name>\n");
        return 2;
    }
    Motion motion(*config);
    printf("{\"variant\":\"%s\",\"dt\":%.9g,\"kin\":{", argv[1], DT);
    printf("\"coxa\":%.9g,\"coxa_offset\":%.9g,\"femur\":%.9g,\"tibia\":%.9g,\"L\":%.9g,\"W\":%.9g},", config->coxa,
           config->coxa_offset, config->femur, config->tibia, config->L, config->W);
    printf("\"ticks\":[");

    int step = 0;
    bool first = true;
    Mode mode = REST;
    Gait gait = TROT;
    CommandMsg cmd = {0, 0, 0, 0, 0, 0, 0};
    bool started = false;
    for (const Segment &segment : SCRIPT) {
        // The order the app's messages arrive in: mode, gait, then the controller input.
        if (!started || segment.mode != mode) motion.setMode(mode = segment.mode);
        if (segment.gait != gait) motion.setGait(gait = segment.gait);
        if (!started || !sameCommand(segment.cmd, cmd)) {
            cmd = segment.cmd;
            motion.state->handleCommand(cmd);
        }
        started = true;
        for (int t = 0; t < segment.ticks; t++, step++) {
            const std::optional<float> gyro = motion.gyro;
            motion.update(segment.imu_x, segment.imu_y, gait == CRAWL, step);
            const body_state_t &b = motion.body;
            printf("%s{\"step\":%d,\"mode\":%d,\"gait\":%d,\"walked\":%d,", first ? "" : ",", step, mode, gait,
                   static_cast<int>(motion.walk.gait()));
            printf("\"cmd\":{\"lx\":%.9g,\"ly\":%.9g,\"rx\":%.9g,\"ry\":%.9g,\"h\":%.9g,\"s\":%.9g,\"s1\":%.9g},", cmd.lx,
                   cmd.ly, cmd.rx, cmd.ry, cmd.h, cmd.s, cmd.s1);
            printf("\"imu\":[%.9g,%.9g],", segment.imu_x, segment.imu_y);
            printf("\"body\":{\"omega\":%.9g,\"phi\":%.9g,\"psi\":%.9g,\"xm\":%.9g,\"ym\":%.9g,\"zm\":%.9g,\"feet\":[", b.omega,
                   b.phi, b.psi, b.xm, b.ym, b.zm);
            for (int leg = 0; leg < 4; leg++) {
                printf("%s[", leg ? "," : "");
                printFloats(b.feet[leg], 3);
                printf("]");
            }
            printf("]},\"stance\":[%d,%d,%d,%d],", motion.stance[0], motion.stance[1], motion.stance[2],
                   motion.stance[3]);
            const gait_state_t &steps = motion.walk.steps();
            printf("\"walk\":{\"phase\":%.9g,\"step_x\":%.9g,\"step_z\":%.9g,\"step_angle\":%.9g,\"share\":%.9g,"
                   "\"swing_height\":%.9g},",
                   motion.walk.phase(), steps.step_x, steps.step_z, steps.step_angle, motion.walk.strokeShare(),
                   motion.walk.swingHeight());
            const OdometryEstimate &o = motion.odometry.estimate();
            printf("\"odometry\":{\"x\":%.9g,\"y\":%.9g,\"yaw\":%.9g,\"distance\":%.9g},", o.x, o.y, o.yaw, o.distance);
            if (gyro)
                printf("\"gyro\":%.9g,", *gyro);
            else
                printf("\"gyro\":null,");
            const FusedEstimate f = motion.fused.estimate();
            printf("\"fused\":{\"x\":%.9g,\"y\":%.9g,\"yaw\":%.9g,\"distance\":%.9g,", f.x, f.y, f.yaw, f.distance);
            printf("\"kinematic_yaw\":%.9g,\"bias\":%.9g},", f.kinematicYaw, motion.fused.gyroBias());
            printf("\"ik\":[");
            printFloats(motion.new_angles, 12);
            printf("],\"angles\":[");
            printFloats(motion.angles, 12);
            printf("]}");
            first = false;
        }
    }
    printf("]}\n");
    return 0;
}
