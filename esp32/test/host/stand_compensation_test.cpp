// Host test of the stand state's IMU compensation, built and run by test_host_programs.py.
#include <algorithm>
#include <cmath>
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
    using StandState::handleCommand;
    using StandState::step;
    float rollIntegral() const { return omega_integral; }
    float pitchIntegral() const { return psi_integral; }
};

static constexpr float DEG = 0.017453292f;
static constexpr float DT = 0.02f;

// A stand on a floor tilted by (roll, pitch) degrees, its IMU reading the floor's tilt less the body's attitude, as
// measured on the Pico (a positive omega lowers the roll and a positive psi lowers the pitch) and in the simulation.
struct Slope {
    Stand stand;
    body_state_t body;
    float roll = 0, pitch = 0;

    Slope(float roll, float pitch) : roll(roll), pitch(pitch) {
        stand.configure(KIN_CONFIG_SPOTMICRO_ESP32_MINI);
        stand.begin();
    }

    ImuSample imu() const {
        ImuSample sample;
        sample.rpy = {(roll - body.omega) * DEG, (pitch - body.psi) * DEG, 0};
        return sample;
    }

    void run(float seconds, float dt = DT) {
        for (int i = 0; i < static_cast<int>(seconds / dt + 0.5f); i++) {
            stand.updateImuOffsets(imu());
            stand.step(body, dt);
        }
    }
};

// The body angles the stand state settles at for a robot tilted by (roll, pitch), REP-103 radians.
static body_state_t settleAt(float roll, float pitch) {
    ImuSample imu;
    imu.rpy = {roll, pitch, 0};
    Stand stand;
    stand.configure(KIN_CONFIG_SPOTMICRO_ESP32_MINI);
    stand.begin();
    body_state_t body;
    for (int i = 0; i < 1000; i++) {
        stand.updateImuOffsets(imu);
        stand.step(body);
    }
    return body;
}

// Measured on the Pico in STAND (2026-10-01): a positive omega lowered the IMU's REP-103 roll (left side down) and a
// positive psi lowered its pitch (nose up). So both offsets are the measured angle with its sign reversed; with roll
// fed in unreversed the stand drove itself to its 20 degree roll limit.
static void aRobotTiltedLeftSideUpRollsItsBodyBack() {
    const body_state_t body = settleAt(5 * DEG, 0);
    CHECK(body.omega > 1);
    CHECK(body.psi > -0.1f && body.psi < 0.1f);
}

static void aRobotTiltedNoseDownLiftsItsNose() {
    const body_state_t body = settleAt(0, 5 * DEG);
    CHECK(body.psi > 1);
    CHECK(body.omega > -0.1f && body.omega < 0.1f);
}

// The proportional term alone, at gain 1, settled with half the slope left; the integral takes the rest.
static void aSlopeIsLevelledFullyWithoutOvershoot() {
    Slope slope(6, -8);
    float overshoot = 0;
    for (int i = 0; i < 500; i++) {
        slope.run(DT);
        overshoot = std::max({overshoot, -slope.imu().rpy[0] / DEG, slope.imu().rpy[1] / DEG});
    }
    CHECK(std::fabs(slope.imu().rpy[0]) < 0.1f * DEG);
    CHECK(std::fabs(slope.imu().rpy[1]) < 0.1f * DEG);
    CHECK(overshoot < 0.1f);
}

// The stick's tilt is an attitude to hold: with the proportional term alone a flat floor gave half of it.
static void aCommandedTiltIsHeldInFull() {
    Slope flat(0, 0);
    CommandMsg cmd {};
    cmd.ry = 0.5f;
    cmd.h = 0.5f;
    flat.stand.handleCommand(cmd);
    const float commanded = 0.5f * KIN_CONFIG_SPOTMICRO_ESP32_MINI.max_pitch;
    float highest = 0;
    for (int i = 0; i < 500; i++) {
        flat.run(DT);
        highest = std::max(highest, flat.body.psi);
    }
    CHECK(std::fabs(flat.body.psi - commanded) < 0.1f);
    CHECK(highest < commanded + 0.1f);
}

// On a slope steeper than the roll limit the output sits at the limit: the integral stops where the proportional
// term plus it reaches the limit instead of running on to the limit itself, so it lets go fast once the slope ends.
static void aSlopeBeyondTheLimitDoesNotWindTheIntegralUp() {
    Slope steep(30, 0);
    steep.run(10);
    const float limit = KIN_CONFIG_SPOTMICRO_ESP32_MINI.max_roll;
    CHECK(std::fabs(steep.body.omega - limit) < 0.01f);
    CHECK(std::fabs(steep.stand.rollIntegral() - (limit - (30 - limit))) < 0.25f);
}

static void aStateTakingOverStartsWithoutTheIntegral() {
    Slope slope(0, 8);
    slope.run(10);
    CHECK(slope.stand.pitchIntegral() > 3);
    slope.stand.resetSmoothing();
    CHECK(slope.stand.pitchIntegral() == 0);
}

// The first tick after the motion loop was idle can carry a long dt; it integrates no more than MAX_INTEGRATION_DT.
static void aLongGapBetweenTicksDoesNotKickTheIntegral() {
    Slope slope(0, 8);
    slope.run(2, 2);
    CHECK(slope.stand.pitchIntegral() > 0);
    CHECK(slope.stand.pitchIntegral() <= 8 * 0.05f + 1e-4f);
}

int main() {
    aRobotTiltedLeftSideUpRollsItsBodyBack();
    aRobotTiltedNoseDownLiftsItsNose();
    aSlopeIsLevelledFullyWithoutOvershoot();
    aCommandedTiltIsHeldInFull();
    aSlopeBeyondTheLimitDoesNotWindTheIntegralUp();
    aStateTakingOverStartsWithoutTheIntegral();
    aLongGapBetweenTicksDoesNotKickTheIntegral();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
