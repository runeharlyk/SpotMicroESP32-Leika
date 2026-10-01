// Host test of the stand state's IMU compensation, built and run by test_host_programs.py.
#define SPOTMICRO_ESP32_MINI
#include <cstdio>
#include <motion_states/stand_state.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

class Stand : public StandState {
  public:
    using StandState::begin;
    using StandState::step;
};

static constexpr float DEG = 0.017453292f;

// The body angles the stand state settles at for a robot tilted by (roll, pitch), REP-103 radians.
static body_state_t settleAt(float roll, float pitch) {
    ImuSample imu;
    imu.rpy = {roll, pitch, 0};
    Stand stand;
    stand.begin();
    body_state_t body;
    for (int i = 0; i < 1000; i++) {
        stand.updateImuOffsets(imu);
        stand.step(body);
    }
    return body;
}

// Kinematics: omega turns about forward x, as REP-103 roll does; psi turns about the lateral axis, which points to
// the robot's right, so a positive psi lifts the nose and REP-103 pitch (nose down) is minus psi.
static void aRobotTiltedLeftSideUpRollsItsBodyBack() {
    const body_state_t body = settleAt(5 * DEG, 0);
    CHECK(body.omega < -1);
    CHECK(body.psi > -0.1f && body.psi < 0.1f);
}

static void aRobotTiltedNoseDownLiftsItsNose() {
    const body_state_t body = settleAt(0, 5 * DEG);
    CHECK(body.psi > 1);
    CHECK(body.omega > -0.1f && body.omega < 0.1f);
}

int main() {
    aRobotTiltedLeftSideUpRollsItsBodyBack();
    aRobotTiltedNoseDownLiftsItsNose();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
