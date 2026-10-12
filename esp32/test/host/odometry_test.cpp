// Host test of the kinematic odometry, alone and as MotionService feeds it, built and run by test_host_programs.py.
#include <cmath>
#include <cstdio>
#include <motion.h>
#include <odometry.h>
#include <variant.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                                              \
    do {                                                                                                     \
        const double a = (actual), e = (expected);                                                           \
        if (!(std::fabs(a - e) <= (tolerance))) {                                                            \
            std::printf("FAIL %s:%d: %s = %.6f, expected %.6f +- %.6f\n", __FILE__, __LINE__, #actual, a, e, \
                        static_cast<double>(tolerance));                                                     \
            failures++;                                                                                      \
        }                                                                                                    \
    } while (0)

static const KinConfig &PICO = KIN_CONFIG_SPOTMICRO_ESP32_MINI;
constexpr float DT = 0.01f;
constexpr int TICKS_PER_S = 100;
constexpr std::array<bool, 4> ALL_DOWN {true, true, true, true};

// A body that moves along an arc carries floor-fixed feet backwards through its own frame. The odometry must recover
// that arc exactly, whatever subset of the feet is down, as long as two are.
static void aRigidArcIsRecoveredExactly() {
    const float v = 0.1f, omega = 0.8f; // m/s forward and rad/s counter-clockwise: a 12.5 cm radius
    float floor[4][2];                  // the feet on the floor, REP-103, where the body started
    for (int leg = 0; leg < 4; leg++) {
        floor[leg][0] = PICO.default_feet_positions[leg][0];
        floor[leg][1] = -PICO.default_feet_positions[leg][2];
    }
    KinematicOdometry odometry;
    float yaw = 0, x = 0, y = 0;
    const int ticks = 300;
    for (int t = 0; t <= ticks; t++) {
        yaw = omega * DT * t;
        x = v / omega * std::sin(yaw);
        y = v / omega * (1 - std::cos(yaw));
        body_state_t body;
        for (int leg = 0; leg < 4; leg++) {
            const float dx = floor[leg][0] - x, dy = floor[leg][1] - y;
            body.feet[leg][0] = std::cos(yaw) * dx + std::sin(yaw) * dy;
            body.feet[leg][1] = 0;
            body.feet[leg][2] = -(-std::sin(yaw) * dx + std::cos(yaw) * dy);
            body.feet[leg][3] = 1;
        }
        // Alternating diagonal pairs, as a trot lifts them; a lifted foot's position must not matter.
        const bool pairA = (t / 20) % 2 == 0;
        const std::array<bool, 4> stance {pairA || t % 20 < 5, !pairA || t % 20 < 5, !pairA || t % 20 < 5,
                                          pairA || t % 20 < 5};
        for (int leg = 0; leg < 4; leg++)
            if (!stance[leg]) body.feet[leg][0] += 0.05f * std::sin(t * 0.7f);
        odometry.update(body, stance);
    }
    const OdometryEstimate &estimate = odometry.estimate();
    CHECK_NEAR(estimate.x, x, 1e-4);
    CHECK_NEAR(estimate.y, y, 1e-4);
    CHECK_NEAR(estimate.yaw, std::remainder(yaw, 2 * M_PI), 1e-4);
    CHECK_NEAR(estimate.distance, v * DT * ticks, 1e-4);
}

// One foot down shows where the body went but not how it turned: the odometry translates and keeps its heading.
static void oneFootDownTranslatesWithoutTurning() {
    KinematicOdometry odometry;
    body_state_t body;
    body.updateFeet(PICO.default_feet_positions);
    odometry.update(body, ALL_DOWN);
    body.feet[0][0] -= 0.01f;
    body.feet[1][0] += 0.02f; // in the air: ignored
    odometry.update(body, {true, false, false, false});
    CHECK_NEAR(odometry.estimate().x, 0.01, 1e-6);
    CHECK_NEAR(odometry.estimate().y, 0, 1e-6);
    CHECK_NEAR(odometry.estimate().yaw, 0, 1e-6);
}

// Stopping mid-stride, the stand eases a lifted foot down and forward while every state but the walk has all feet in
// stance: in the air, it must not drag the body along; once down, it counts.
static void aFootInTheAirDoesNotCountWhateverThePhase() {
    KinematicOdometry odometry;
    body_state_t body;
    body.updateFeet(PICO.default_feet_positions);
    body.feet[0][1] = 0.01f;
    odometry.update(body, ALL_DOWN);
    body.feet[0][0] += 0.02f;
    body.feet[0][1] = 0.004f;
    odometry.update(body, ALL_DOWN);
    CHECK(odometry.estimate().x == 0 && odometry.estimate().yaw == 0);
    body.feet[0][1] = 0.002f;
    odometry.update(body, ALL_DOWN);
    body.feet[0][0] += 0.02f;
    odometry.update(body, ALL_DOWN);
    CHECK(odometry.estimate().x < -0.001f);
}

static socket_message_ControllerData sticks(float lx, float ly, float rx) {
    socket_message_ControllerData data = socket_message_ControllerData_init_zero;
    data.has_left = data.has_right = true;
    data.left.x = lx;
    data.left.y = ly;
    data.right.x = rx;
    data.height = 0.5f;
    data.speed = 0.5f;
    data.s1 = 0.2f;
    return data;
}

// The input is re-sent every tick, as the app does while a stick is off centre, so the dead-man stop stays away.
static void run(MotionService &motion, const socket_message_ControllerData &input, int ticks) {
    for (int i = 0; i < ticks; i++) {
        motion.inbox.postInput(input, fake_timer::nowUs / 1000, fake_timer::nowUs);
        fake_timer::nowUs += 10000;
        motion.update(ImuSample {}, eGestureNone);
    }
}

static void enter(MotionService &motion, socket_message_ModesEnum mode) {
    motion.inbox.postMode(mode);
    run(motion, sticks(0, 0, 0), 1);
}

static void walkingPico(MotionService &motion, socket_message_WalkGaits gait = socket_message_WalkGaits_TROT) {
    motion.useConfig(&PICO);
    motion.begin();
    enter(motion, socket_message_ModesEnum_STAND);
    run(motion, sticks(0, 0, 0), 100);
    motion.inbox.postGait(gait);
    enter(motion, socket_message_ModesEnum_WALK);
}

// The stance sweeps a foot through the whole stroke in the stance part of the cycle, so the body moves at
// stroke x cycle rate / duty: STICK x max_step_length x (0.5 x 2) / 0.75 in trot at the slowest cadence. A stick
// short of full and a low step height (sticks()), so the walk neither slows its clock for the servos' speed, which
// the Pico's lift alone at the app's step height of 0.5 does, nor bounds the stroke to the legs' reach.
static constexpr float STICK = 0.3f;
static const float TROT_SPEED = STICK * PICO.max_step_length * 1.0f / 0.75f;
static const float CRAWL_SPEED = STICK * PICO.max_step_length * 0.25f / 0.85f;

// Over whole gait cycles, after the 333 ms command filter has settled.
static OdometryEstimate walked(MotionService &motion, const socket_message_ControllerData &input, int seconds) {
    run(motion, input, TICKS_PER_S);
    const OdometryEstimate start = motion.odometry();
    run(motion, input, seconds * TICKS_PER_S);
    OdometryEstimate end = motion.odometry();
    // Expressed in the frame the robot faced at the start, so a heading picked up while settling does not rotate it.
    const float dx = end.x - start.x, dy = end.y - start.y, c = std::cos(start.yaw), s = std::sin(start.yaw);
    return {c * dx + s * dy, -s * dx + c * dy, std::remainder(end.yaw - start.yaw, 2.0f * static_cast<float>(M_PI)),
            end.distance - start.distance};
}

static void aForwardTrotGoesStraightAtTheStanceSpeed() {
    MotionService motion;
    walkingPico(motion);
    const OdometryEstimate leg = walked(motion, sticks(0, STICK, 0), 3);
    CHECK_NEAR(leg.x, 3 * TROT_SPEED, 0.005 * 3 * TROT_SPEED);
    CHECK_NEAR(leg.y, 0, 1e-4);
    CHECK_NEAR(leg.yaw, 0, 1e-4);
    CHECK_NEAR(leg.distance, leg.x, 1e-4);
}

// The left stick to the right steps to the right (speed_model.py: vy = lx x speed, and REP-103's y is left).
static void aSidewaysTrotGoesSidewaysAtTheStanceSpeed() {
    MotionService motion;
    walkingPico(motion);
    const OdometryEstimate leg = walked(motion, sticks(STICK, 0, 0), 3);
    CHECK_NEAR(leg.x, 0, 1e-4);
    CHECK_NEAR(leg.y, 3 * TROT_SPEED, 0.005 * 3 * TROT_SPEED);
    CHECK_NEAR(leg.yaw, 0, 1e-4);
}

// The crawl sways the body over the three feet that stay down; over whole cycles the sway cancels, and the path it
// adds shows only in the distance.
static void aForwardCrawlGoesStraightOverWholeCycles() {
    MotionService motion;
    walkingPico(motion, socket_message_WalkGaits_CRAWL);
    const OdometryEstimate leg = walked(motion, sticks(0, STICK, 0), 8); // two cycles at a quarter cycle per second
    CHECK_NEAR(leg.x, 8 * CRAWL_SPEED, 0.01 * 8 * CRAWL_SPEED);
    CHECK_NEAR(leg.y, 0, 1e-3);
    CHECK(leg.distance > leg.x);
}

// The right stick to the right turns clockwise. Each foot strokes along a straight chord as long as step_angle x its
// radius, so it sweeps 2 atan(STICK / 2) about the centre per stance where speed_model.py's linear model says STICK
// rad: the turn rate lies a little under STICK x rate / duty.
static void aTurnInPlaceTurnsClockwiseWithoutTravelling() {
    MotionService motion;
    walkingPico(motion);
    const OdometryEstimate leg = walked(motion, sticks(0, 0, STICK), 2);
    const float linearModel = -STICK / 0.75f;
    CHECK(leg.yaw / 2 < 0.92f * linearModel);
    CHECK(leg.yaw / 2 > 1.01f * linearModel);
    CHECK_NEAR(leg.x, 0, 2e-3);
    CHECK_NEAR(leg.y, 0, 2e-3);
}

static void standingStill() {
    MotionService motion;
    walkingPico(motion);
    run(motion, sticks(0, 0, 0), 2 * TICKS_PER_S);
    const OdometryEstimate still = motion.odometry();
    CHECK(still.x == 0 && still.y == 0 && still.yaw == 0 && still.distance == 0);

    // Leaning the body forward in stand moves it over the feet, and leaning back undoes it.
    enter(motion, socket_message_ModesEnum_STAND);
    run(motion, sticks(0, 1, 0), 2 * TICKS_PER_S);
    CHECK_NEAR(motion.odometry().x, PICO.max_body_shift_x, 1e-4);
    run(motion, sticks(0, 0, 0), 2 * TICKS_PER_S);
    CHECK_NEAR(motion.odometry().x, 0, 1e-4);

    // So does twisting it: the stand's phi turns the body counter-clockwise about the vertical, over planted feet.
    run(motion, sticks(0, 0, 1), 2 * TICKS_PER_S);
    CHECK_NEAR(motion.odometry().yaw, PICO.max_roll * DEG2RAD_F, 1e-4);
    CHECK_NEAR(motion.odometry().x, 0, 1e-4);
    run(motion, sticks(0, 0, 0), 2 * TICKS_PER_S);
    CHECK_NEAR(motion.odometry().yaw, 0, 1e-4);
}

static void resetting() {
    MotionService motion;
    walkingPico(motion);
    run(motion, sticks(0, STICK, 0), 2 * TICKS_PER_S);
    const OdometryEstimate before = motion.odometry();
    CHECK(before.x > 0.04f);
    const OdometryEstimate reached = motion.resetOdometry();
    CHECK(reached.x == before.x && reached.distance == before.distance);
    const OdometryEstimate after = motion.odometry();
    CHECK(after.x == 0 && after.y == 0 && after.yaw == 0 && after.distance == 0);

    // It counts on from the reset at the same pace, and a deactivation starts it afresh too.
    run(motion, sticks(0, STICK, 0), TICKS_PER_S);
    CHECK_NEAR(motion.odometry().x, TROT_SPEED, 0.01 * TROT_SPEED);
    enter(motion, socket_message_ModesEnum_DEACTIVATED);
    const OdometryEstimate deactivated = motion.odometry();
    CHECK(deactivated.x == 0 && deactivated.distance == 0);
}

int main() {
    aRigidArcIsRecoveredExactly();
    oneFootDownTranslatesWithoutTurning();
    aForwardTrotGoesStraightAtTheStanceSpeed();
    aSidewaysTrotGoesSidewaysAtTheStanceSpeed();
    aForwardCrawlGoesStraightOverWholeCycles();
    aTurnInPlaceTurnsClockwiseWithoutTravelling();
    standingStill();
    aFootInTheAirDoesNotCountWhateverThePhase();
    resetting();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
