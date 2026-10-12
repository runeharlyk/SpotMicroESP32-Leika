// Host test of the kinematic and fused odometry, alone and as MotionService feeds them, built and run by
// test_host_programs.py.
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
        floor[leg][1] = PICO.default_feet_positions[leg][2];
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
            body.feet[leg][2] = -std::sin(yaw) * dx + std::cos(yaw) * dy;
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

// The body of a robot standing at (x, y, yaw) on feet planted where it stood at the origin, all four down.
static body_state_t bodyAt(float x, float y, float yaw) {
    body_state_t body;
    for (int leg = 0; leg < 4; leg++) {
        const float dx = PICO.default_feet_positions[leg][0] - x, dy = PICO.default_feet_positions[leg][2] - y;
        body.feet[leg][0] = std::cos(yaw) * dx + std::sin(yaw) * dy;
        body.feet[leg][1] = 0;
        body.feet[leg][2] = -std::sin(yaw) * dx + std::cos(yaw) * dy;
        body.feet[leg][3] = 1;
    }
    return body;
}

// A gyro's white noise, the same every run: the sum of twelve uniforms is near enough normal.
struct Noise {
    uint32_t state = 12345;
    float operator()(float sigma) {
        float sum = -6;
        for (int i = 0; i < 12; i++) sum += (state = state * 1664525u + 1013904223u) / 4294967296.0f;
        return sigma * sum;
    }
};

/**
 * A robot that walks at `speed` m/s forward and turns at `rate` rad/s, while its feet show `feetScale` of the turn,
 * and its gyro reads the turn plus `bias`, the measured noise and a trot's rocking of `rock` rad/s at 2 Hz, which the
 * feet do not show and the body really does. The truth integrates the turn the gyro sees, without the bias and noise.
 */
struct TestRobot {
    float speed = 0, rate = 0, feetScale = 1, bias = 0, rock = 0;
    float x = 0, y = 0, yaw = 0;             // the truth
    float feetX = 0, feetY = 0, feetYaw = 0; // where the feet say it is
    float t = 0, gyroYaw = 0;                // the gyro's own integral, bias and all
    Noise noise;

    void tick(FusedOdometry &odometry, bool withGyro = true) {
        const float turn = rate + rock * std::sin(2 * static_cast<float>(M_PI) * 2 * t);
        x += std::cos(yaw) * speed * DT, y += std::sin(yaw) * speed * DT, yaw += turn * DT;
        feetX += std::cos(feetYaw) * speed * DT, feetY += std::sin(feetYaw) * speed * DT;
        feetYaw += feetScale * rate * DT;
        const float gyro = turn + bias + noise(FusedOdometry::GYRO_NOISE);
        gyroYaw += gyro * DT;
        t += DT;
        odometry.update(bodyAt(feetX, feetY, feetYaw), ALL_DOWN, withGyro ? std::optional<float>(gyro) : std::nullopt,
                        DT);
    }
};

static float angleBetween(float a, float b) { return std::remainder(a - b, 2.0f * static_cast<float>(M_PI)); }

// A still robot shows its gyro's bias outright: the filter learns it within a second, and follows it when it moves.
static void aStillRobotLearnsItsGyroBias() {
    FusedOdometry odometry;
    TestRobot robot;
    robot.bias = 0.03f;
    for (int t = 0; t < TICKS_PER_S; t++) robot.tick(odometry);
    CHECK_NEAR(odometry.gyroBias(), 0.03, 1e-3);
    robot.bias = -0.01f;
    for (int t = 0; t < 20 * TICKS_PER_S; t++) robot.tick(odometry);
    CHECK_NEAR(odometry.gyroBias(), -0.01, 1e-3);
    CHECK_NEAR(odometry.estimate().yaw, 0, 0.005);
}

// The bias a boot calibration leaves turns a gyro's heading by 0.6 rad in a minute; the feet hold the fused one.
static void standingStillDriftsLessThanTheGyro() {
    FusedOdometry odometry;
    TestRobot robot;
    robot.bias = 0.01f;
    for (int t = 0; t < 60 * TICKS_PER_S; t++) robot.tick(odometry);
    CHECK_NEAR(robot.gyroYaw, 0.6, 0.01);
    CHECK_NEAR(odometry.estimate().yaw, 0, 0.002);
    std::printf("standing a minute: the gyro drifts %.3f rad, the fused heading %.5f rad\n", robot.gyroYaw,
                odometry.estimate().yaw);
}

// Feet that undercount a turn by 13 %, as in MuJoCo, under a trot's rocking: the gyro sets the fused heading, and the
// rocking and the undercount barely move the bias learnt standing.
static void aTurnTheFeetUndercountIsCorrectedByTheGyro() {
    FusedOdometry odometry;
    TestRobot robot;
    robot.bias = 0.02f;
    for (int t = 0; t < 2 * TICKS_PER_S; t++) robot.tick(odometry);
    odometry.reset();
    robot.yaw = robot.feetYaw = 0;
    robot.rate = 0.8f, robot.feetScale = 0.87f, robot.rock = 0.08f;
    for (int t = 0; t < 4 * TICKS_PER_S; t++) robot.tick(odometry);
    const FusedEstimate e = odometry.estimate();
    CHECK_NEAR(e.kinematicYaw, std::remainder(0.87 * 3.2, 2 * M_PI), 0.01);
    CHECK_NEAR(angleBetween(e.yaw, robot.yaw), 0, 0.01 * 3.2);
    CHECK_NEAR(odometry.gyroBias(), 0.02, 0.003);
    std::printf("a 3.2 rad turn: the feet %.3f rad off, the fused heading %.4f rad\n",
                angleBetween(e.kinematicYaw, robot.yaw), angleBetween(e.yaw, robot.yaw));
}

// The position follows the fused heading: on an arc the feet undercount, it stays with the truth, on the bias it learnt
// standing before it walked.
static void walkingAnArcFollowsTheFusedHeading() {
    FusedOdometry odometry;
    TestRobot robot;
    robot.bias = -0.02f;
    for (int t = 0; t < TICKS_PER_S; t++) robot.tick(odometry);
    robot.speed = 0.1f, robot.rate = 0.3f, robot.feetScale = 0.87f, robot.rock = 0.08f;
    for (int t = 0; t < 6 * TICKS_PER_S; t++) robot.tick(odometry);
    const FusedEstimate e = odometry.estimate();
    const float fusedError = std::hypot(e.x - robot.x, e.y - robot.y);
    const float feetError = std::hypot(robot.feetX - robot.x, robot.feetY - robot.y);
    CHECK(feetError > 0.03f);
    CHECK(fusedError < 0.1f * feetError);
    CHECK_NEAR(e.distance, 0.6, 1e-3);
    std::printf("a 0.6 m arc: the feet end %.1f mm off, the fused estimate %.1f mm\n", 1000 * feetError,
                1000 * fusedError);
}

// Walking feet would teach the bias their undercount: a robot that never stood still learns no bias.
static void walkingTeachesTheBiasNothing() {
    FusedOdometry odometry;
    TestRobot robot;
    robot.speed = 0.1f, robot.rate = 0.8f, robot.feetScale = 0.87f, robot.rock = 0.08f;
    for (int t = 0; t < 4 * TICKS_PER_S; t++) robot.tick(odometry);
    CHECK(odometry.gyroBias() == 0);
    CHECK(std::fabs(angleBetween(odometry.estimate().yaw, robot.yaw)) <
          std::fabs(angleBetween(odometry.estimate().kinematicYaw, robot.yaw)));
}

// Without a gyro rate, or over a tick too long to trust one, it is the kinematic odometry.
static void withoutAGyroItIsTheKinematicOdometry() {
    FusedOdometry fused;
    KinematicOdometry kinematic;
    TestRobot robot;
    robot.speed = 0.1f, robot.rate = 0.5f, robot.bias = 0.05f;
    for (int t = 0; t < 2 * TICKS_PER_S; t++) {
        robot.tick(fused, false);
        kinematic.update(bodyAt(robot.feetX, robot.feetY, robot.feetYaw), ALL_DOWN);
    }
    const body_state_t late = bodyAt(robot.feetX + 0.01f, robot.feetY, robot.feetYaw + 0.1f);
    fused.update(late, ALL_DOWN, 3.0f, 1.0f);
    kinematic.update(late, ALL_DOWN);
    const FusedEstimate e = fused.estimate();
    const OdometryEstimate &k = kinematic.estimate();
    CHECK_NEAR(e.x, k.x, 1e-6);
    CHECK_NEAR(e.y, k.y, 1e-6);
    CHECK_NEAR(e.distance, k.distance, 1e-6);
    CHECK(e.yaw == k.yaw && e.kinematicYaw == k.yaw);
    CHECK(fused.gyroBias() == 0);
}

// A sample counts only with a gyro reading, and only while the sensor task keeps it fresh.
static void aStaleOrMissingGyroIsNone() {
    ImuSample imu;
    imu.t_us = 1000000;
    imu.gyro = {0.1f, 0.2f, 0.3f};
    CHECK(!FusedOdometry::yawRate(imu, 1000000));
    imu.valid = ImuValid::ACCEL | ImuValid::GYRO;
    CHECK(FusedOdometry::yawRate(imu, 1000000 + FusedOdometry::GYRO_STALE_US) == 0.3f);
    CHECK(!FusedOdometry::yawRate(imu, 1000001 + FusedOdometry::GYRO_STALE_US));
}

// The first tick, or one with fewer than two feet down, shows no turn: the gyro alone turns it, and the bias stays.
static void aTurnTheFeetDidNotSeeIsTheGyros() {
    FusedOdometry odometry;
    body_state_t body;
    body.updateFeet(PICO.default_feet_positions);
    odometry.update(body, ALL_DOWN, 0.3f, DT);
    CHECK_NEAR(odometry.estimate().yaw, 0.3f * DT, 1e-7);
    body.feet[0][0] -= 0.01f;
    odometry.update(body, {true, false, false, false}, 0.3f, DT);
    CHECK_NEAR(odometry.estimate().yaw, 0.6f * DT, 1e-7);
    CHECK_NEAR(odometry.estimate().x, 0.01, 1e-6);
    CHECK(odometry.gyroBias() == 0);
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

// The input is re-sent every tick, as the app does while a stick is off centre, so the dead-man stop stays away. With
// a gyro rate, the IMU sample carries it, read `sampleAgeUs` before the tick.
static void run(MotionService &motion, const socket_message_ControllerData &input, int ticks,
                std::optional<float> gyroRate = std::nullopt, int64_t sampleAgeUs = 0) {
    for (int i = 0; i < ticks; i++) {
        motion.inbox.postInput(input, fake_timer::nowUs / 1000, fake_timer::nowUs);
        fake_timer::nowUs += 10000;
        ImuSample imu;
        if (gyroRate) {
            imu.valid = ImuValid::ACCEL | ImuValid::GYRO;
            imu.gyro = {0, 0, *gyroRate};
            imu.t_us = fake_timer::nowUs - sampleAgeUs;
        }
        motion.update(imu, eGestureNone);
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

// The left stick to the right steps to the right (step_z = -lx: the left legs mount at +z), and REP-103's y is left.
static void aSidewaysTrotGoesSidewaysAtTheStanceSpeed() {
    MotionService motion;
    walkingPico(motion);
    const OdometryEstimate leg = walked(motion, sticks(STICK, 0, 0), 3);
    CHECK_NEAR(leg.x, 0, 1e-4);
    CHECK_NEAR(leg.y, -3 * TROT_SPEED, 0.005 * 3 * TROT_SPEED);
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

// A positive rx turns counter-clockwise: each foot strokes along step_angle x (-z, x), from forward towards the left.
// It strokes along a straight chord as long as step_angle x its radius, so it sweeps 2 atan(STICK / 2) about the
// centre per stance where speed_model.py's linear model says STICK rad: the turn rate lies a little under
// STICK x rate / duty.
static void aTurnInPlaceTurnsCounterClockwiseWithoutTravelling() {
    MotionService motion;
    walkingPico(motion);
    const OdometryEstimate leg = walked(motion, sticks(0, 0, STICK), 2);
    const float linearModel = STICK / 0.75f;
    CHECK(leg.yaw / 2 > 0.92f * linearModel);
    CHECK(leg.yaw / 2 < 1.01f * linearModel);
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

    // So does twisting it: a positive phi turns the body clockwise about the vertical (YAW_TO_PHI), over planted feet.
    run(motion, sticks(0, 0, 1), 2 * TICKS_PER_S);
    CHECK_NEAR(motion.odometry().yaw, -PICO.max_roll * DEG2RAD_F, 1e-4);
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

// A robot whose feet turn on the spot while its gyro shows only its bias, as one held in the hand: the fused heading
// stays, the feet's own turns on. A sample the sensor task stopped renewing counts as no gyro.
static void theServiceFusesAFreshGyro() {
    MotionService motion;
    walkingPico(motion);
    run(motion, sticks(0, 0, 0), TICKS_PER_S, 0.02f);
    motion.resetOdometry();
    run(motion, sticks(0, 0, 1), 2 * TICKS_PER_S, 0.02f);
    const FusedEstimate held = motion.odometry();
    CHECK(held.kinematicYaw > 1.5f);
    CHECK_NEAR(held.yaw, 0, 0.02);

    motion.resetOdometry();
    run(motion, sticks(0, 0, 1), TICKS_PER_S, 0.02f, FusedOdometry::GYRO_STALE_US + 10000);
    const FusedEstimate stale = motion.odometry();
    CHECK(stale.kinematicYaw > 0.75f && stale.yaw == stale.kinematicYaw);
}

// The feet and a gyro read the same turn: on a counter-clockwise turn by a positive rx, a gyro in the body frame
// (x forward, y left, z up) reads a positive yaw rate, and the feet's heading must turn the same way and as far, or the
// fused heading would follow one of two opposite turns.
static void aTurnByAPositiveRxIsCounterClockwiseToTheFeetAndTheGyro() {
    MotionService motion;
    walkingPico(motion);
    const float gyroRate = 0.96f * STICK / 0.75f; // the turn the stroke chords give (above)
    run(motion, sticks(0, 0, STICK), TICKS_PER_S, gyroRate);
    const FusedEstimate start = motion.odometry();
    run(motion, sticks(0, 0, STICK), 2 * TICKS_PER_S, gyroRate);
    const FusedEstimate end = motion.odometry();
    const float feet = std::remainder(end.kinematicYaw - start.kinematicYaw, 2 * M_PI);
    const float fused = std::remainder(end.yaw - start.yaw, 2 * M_PI);
    CHECK(feet > 0);
    CHECK_NEAR(feet, 2 * gyroRate, 0.05 * 2 * gyroRate);
    CHECK_NEAR(fused, 2 * gyroRate, 0.02 * 2 * gyroRate);
    CHECK_NEAR(motion.odometry().yaw, motion.odometry().kinematicYaw, 0.05 * std::fabs(motion.odometry().yaw));
}

int main() {
    aRigidArcIsRecoveredExactly();
    oneFootDownTranslatesWithoutTurning();
    aForwardTrotGoesStraightAtTheStanceSpeed();
    aSidewaysTrotGoesSidewaysAtTheStanceSpeed();
    aForwardCrawlGoesStraightOverWholeCycles();
    aTurnInPlaceTurnsCounterClockwiseWithoutTravelling();
    standingStill();
    aFootInTheAirDoesNotCountWhateverThePhase();
    resetting();
    aStillRobotLearnsItsGyroBias();
    standingStillDriftsLessThanTheGyro();
    aTurnTheFeetUndercountIsCorrectedByTheGyro();
    walkingAnArcFollowsTheFusedHeading();
    walkingTeachesTheBiasNothing();
    withoutAGyroItIsTheKinematicOdometry();
    aStaleOrMissingGyroIsNone();
    aTurnTheFeetDidNotSeeIsTheGyros();
    theServiceFusesAFreshGyro();
    aTurnByAPositiveRxIsCounterClockwiseToTheFeetAndTheGyro();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
