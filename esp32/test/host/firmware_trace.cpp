// Replays a fixed control script through the firmware's own motion code and prints the result as
// JSON, so the web app's TypeScript port (app/src/lib/simulation/firmware) can be pinned to it.
// Built once per kinematics variant by export_firmware_traces.py.
#include <cmath>
#include <cstdio>
#include <motion_states/rest_state.h>
#include <motion_states/stand_state.h>
#include <motion_states/walk_state.h>

namespace {

enum Mode { REST = 3, STAND = 4, WALK = 5 };
enum Gait { TROT = 0, CRAWL = 1 };

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
    {WALK, TROT, {0, 1, 0, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 200},
    {WALK, TROT, {-1, 0, 0, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 100},
    {WALK, TROT, {0, 0, 1, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 100},
    {WALK, TROT, {0, 0, 0, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 50},
    {WALK, CRAWL, {0, 0.6f, 0, 0, 0.5f, 0.5f, 0.5f}, 0, 0, 150},
    {STAND, CRAWL, {0, 0, 0, 0, 0.5f, 0, 0}, 0, 0, 50},
};

// MotionService's state and angle handling (esp32/src/motion.cpp), without its timer and peripherals.
struct Motion {
    RestState rest;
    StandState stand;
    WalkState walk;
    Kinematics kinematics;
    MotionState *state = nullptr;
    body_state_t body;
    float new_angles[12] = {0};
    float angles[12] = {0};
    const float dir[12] = {1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1};

    Motion() { body.updateFeet(KinConfig::default_feet_positions); }

    void setMode(Mode mode) {
        if (state) state->end();
        MotionState *states[] = {&rest, &stand, &walk};
        state = states[mode - REST];
        state->begin();
    }

    void setGait(Gait gait) {
        if (gait == TROT)
            walk.set_mode_trot();
        else
            walk.set_mode_crawl();
    }

    void update(float imu_x, float imu_y) {
        state->updateImuOffsets(imu_y, imu_x);
        state->step(body, DT);
        kinematics.calculate_inverse_kinematics(body, new_angles);
        for (int i = 0; i < 12; i++) {
            const float angle = new_angles[i] * dir[i];
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

}  // namespace

int main(int argc, char **argv) {
    Motion motion;
    // The variant is chosen with -D at compile time; the exporter passes its name for the record.
    printf("{\"variant\":\"%s\",\"dt\":%.9g,\"kin\":{", argc > 1 ? argv[1] : "", DT);
    printf("\"coxa\":%.9g,\"coxa_offset\":%.9g,\"femur\":%.9g,\"tibia\":%.9g,\"L\":%.9g,\"W\":%.9g},", KinConfig::coxa,
           KinConfig::coxa_offset, KinConfig::femur, KinConfig::tibia, KinConfig::L, KinConfig::W);
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
            motion.update(segment.imu_x, segment.imu_y);
            const body_state_t &b = motion.body;
            printf("%s{\"step\":%d,\"mode\":%d,\"gait\":%d,", first ? "" : ",", step, mode, gait);
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
            printf("]},\"angles\":[");
            printFloats(motion.angles, 12);
            printf("]}");
            first = false;
        }
    }
    printf("]}\n");
    return 0;
}
