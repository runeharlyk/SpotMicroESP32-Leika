// Host test of the goals MotionService drives on the Pico's odometry, built and run by test_host_programs.py.
#include <cmath>
#include <cstdio>
#include <motion.h>
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
constexpr int TICKS_PER_S = 100;
constexpr float DEG = static_cast<float>(M_PI) / 180.0f;

// A client of one link, and a client with the same id on another: ids repeat across links.
static int someLink, otherLink;
static const LinkClient CLIENT {&someLink, 3}, SAME_ID_ELSEWHERE {&otherLink, 3};

static void tick(MotionService &motion, int ticks = 1) {
    for (int i = 0; i < ticks; i++) {
        fake_timer::nowUs += 10000;
        motion.update(ImuSample {}, eGestureNone);
    }
}

static socket_message_ControllerData sticks(float lx, float ly, float height = 0.5f) {
    socket_message_ControllerData data = socket_message_ControllerData_init_zero;
    data.has_left = data.has_right = true;
    data.left.x = lx;
    data.left.y = ly;
    data.height = height;
    data.speed = 0.5f;
    data.s1 = 0.5f;
    return data;
}

static void input(MotionService &motion, const socket_message_ControllerData &data) {
    motion.inbox.postInput(data, fake_timer::nowUs / 1000, fake_timer::nowUs);
    tick(motion);
}

// A Pico standing still, as after the app stood it up, unless `driven` is false: then no app ever sent input.
static void standingPico(MotionService &motion, bool driven = true) {
    motion.useConfig(&PICO);
    motion.begin();
    motion.inbox.postMode(socket_message_ModesEnum_STAND);
    tick(motion);
    if (driven) input(motion, sticks(0, 0));
    tick(motion, TICKS_PER_S);
}

static GoalState runGoal(MotionService &motion, int maxSeconds = 30) {
    for (int i = 0; i < maxSeconds * TICKS_PER_S && motion.goalStatus().state == GoalState::RUNNING; i++) tick(motion);
    return motion.goalStatus().state;
}

static bool centred(const MotionService &motion) {
    const CommandMsg &command = motion.currentCommand();
    return command.lx == 0 && command.ly == 0 && command.rx == 0 && command.ry == 0;
}

// Through the real walk, the command filter and the odometry: once the robot has settled in STAND it ends well within
// the goal's tolerance (measured: 0 to 1.1 mm, and 0.1 to 0.6 degrees), and leaves the odometry's other axes alone.
constexpr float SETTLED_M = 0.003f;
constexpr float SETTLED_RAD = 1 * DEG;
static int sign(float value) { return (value > 0) - (value < 0); }
static void aGoalWalksTheOdometryToItsTarget() {
    struct Case {
        Goal goal;
        const char *name;
    };
    const Case cases[] = {
        {{0.2f, 0, 0, 0.5f}, "0.2 m forward"},
        {{0, -0.1f, 0, 0.5f}, "0.1 m to the right"},
        {{-0.05f, 0, 0, 1}, "5 cm back at full speed"},
        {{0, 0, 90 * DEG, 0.5f}, "90 degrees counter-clockwise"},
        {{0, 0, -30 * DEG, 0.3f}, "30 degrees clockwise"},
    };
    for (const Case &c : cases) {
        MotionService motion;
        standingPico(motion);
        const uint32_t id = motion.inbox.postGoal(c.goal);
        tick(motion);
        CHECK(motion.goalStatus().id == id);
        CHECK(motion.goalStatus().state == GoalState::RUNNING);
        CHECK(motion.mode() == socket_message_ModesEnum_WALK);
        CHECK(!centred(motion)); // the goal's sticks are the command in force, as the app's would be
        // On the sticks as the walk takes them: ly forward, lx to the right, rx counter-clockwise. A straight goal's
        // other sticks hold its line and heading.
        const CommandMsg &command = motion.currentCommand();
        if (c.goal.forward != 0) CHECK(sign(command.ly) == sign(c.goal.forward));
        if (c.goal.sideways != 0) CHECK(sign(command.lx) == -sign(c.goal.sideways));
        if (c.goal.turn != 0) CHECK(sign(command.rx) == sign(c.goal.turn) && command.lx == 0 && command.ly == 0);
        const GoalState end = runGoal(motion);
        CHECK(end == GoalState::REACHED);
        CHECK(motion.mode() == socket_message_ModesEnum_STAND);
        CHECK(centred(motion));
        tick(motion, 2 * TICKS_PER_S);
        const OdometryEstimate at = motion.odometry();
        if (c.goal.turn != 0) {
            CHECK_NEAR(at.yaw, c.goal.turn, SETTLED_RAD);
            CHECK_NEAR(std::hypot(at.x, at.y), 0, 0.01);
            std::printf("%s: %.2f degrees off after settling\n", c.name, (at.yaw - c.goal.turn) / DEG);
        } else {
            CHECK_NEAR(at.x, c.goal.forward, SETTLED_M);
            CHECK_NEAR(at.y, c.goal.sideways, SETTLED_M);
            CHECK_NEAR(at.yaw, 0, 1e-3);
            std::printf("%s: %.1f mm off after settling\n", c.name,
                        1000 * std::hypot(at.x - c.goal.forward, at.y - c.goal.sideways));
        }
    }
}

// The goal brings the app's defaults, rather than a body at its lowest that cannot lift a foot.
static void aRobotNoAppHasDrivenStillWalks() {
    MotionService motion;
    standingPico(motion, false);
    motion.inbox.postGoal({0.1f, 0, 0, 0.5f});
    tick(motion);
    CHECK(motion.currentCommand().h == 0.7f && motion.currentCommand().s1 == 0.5f);
    CHECK(runGoal(motion) == GoalState::REACHED);
    CHECK_NEAR(motion.odometry().x, 0.1f, 0.01);
}

static void aStickOffCentreTakesOverAndACentredOneDoesNot() {
    MotionService motion;
    standingPico(motion);
    motion.inbox.postGoal({0.3f, 0, 0, 0.5f});
    tick(motion, 50);
    input(motion, sticks(0, 0, 0.9f)); // only the height
    CHECK(motion.goalStatus().state == GoalState::RUNNING);
    CHECK(motion.currentCommand().h == 0.9f && motion.currentCommand().ly > 0);
    input(motion, sticks(0.4f, 0));
    CHECK(motion.goalStatus().state == GoalState::CANCELLED);
    CHECK(motion.currentCommand().lx == 0.4f && motion.currentCommand().ly == 0);
    CHECK(motion.mode() == socket_message_ModesEnum_WALK);
}

// Anything else that ends it centres the sticks: a mode would read them as a lean, a walk would carry on.
static void aCancelAModeOrAGaitEndsIt() {
    {
        MotionService motion;
        standingPico(motion);
        motion.inbox.postGoal({0.3f, 0, 0, 0.5f});
        tick(motion, 50);
        motion.inbox.postGoalCancel();
        tick(motion);
        CHECK(motion.goalStatus().state == GoalState::CANCELLED);
        CHECK(centred(motion));
        CHECK(motion.mode() == socket_message_ModesEnum_WALK);
        const float progress = motion.goalStatus().progress;
        CHECK(progress > 0.01f);
        tick(motion, TICKS_PER_S);
        CHECK(motion.goalStatus().progress == progress);
    }
    {
        MotionService motion;
        standingPico(motion);
        motion.inbox.postGoal({0, 0, 1, 0.5f});
        tick(motion, 50);
        motion.inbox.postMode(socket_message_ModesEnum_STAND);
        tick(motion);
        CHECK(motion.goalStatus().state == GoalState::CANCELLED);
        CHECK(centred(motion));
        CHECK(motion.mode() == socket_message_ModesEnum_STAND);
    }
    {
        MotionService motion;
        standingPico(motion);
        motion.inbox.postGoal({0.3f, 0, 0, 0.5f});
        tick(motion, 50);
        motion.inbox.postGait(socket_message_WalkGaits_CRAWL);
        tick(motion);
        CHECK(motion.goalStatus().state == GoalState::CANCELLED);
        CHECK(centred(motion));
    }
}

// A USB serial port the host closed is not reported: a goal whose client falls silent for OWNER_SILENCE_MS ends, with
// its sticks centred. Any frame from the client keeps it going.
static void itEndsWhenTheClientThatStartedItFallsSilent() {
    MotionService motion;
    standingPico(motion);
    motion.inbox.postGoal({0.3f, 0, 0, 0.5f}, CLIENT, fake_timer::nowUs / 1000);
    for (int i = 0; i < 6; i++) {
        tick(motion, TICKS_PER_S / 2);
        motion.inbox.heard(CLIENT, fake_timer::nowUs / 1000);
    }
    tick(motion, MotionInbox::OWNER_SILENCE_MS / 10 - 1);
    CHECK(motion.goalStatus().state == GoalState::RUNNING);
    tick(motion, 2);
    CHECK(motion.goalStatus().state == GoalState::CANCELLED);
    CHECK(centred(motion));
}

static void itEndsWhenTheClientThatStartedItLeaves() {
    MotionService motion;
    standingPico(motion);
    const uint32_t id = motion.inbox.postGoal({0.3f, 0, 0, 0.5f}, CLIENT, fake_timer::nowUs / 1000);
    tick(motion, 50);
    motion.inbox.dropClient(SAME_ID_ELSEWHERE);
    tick(motion);
    CHECK(motion.goalStatus().state == GoalState::RUNNING);
    motion.inbox.dropClient(CLIENT);
    tick(motion);
    CHECK(motion.goalStatus().state == GoalState::CANCELLED);
    CHECK(motion.goalStatus().id == id);
    CHECK(centred(motion));
}

// The dead-man stop guards the app's sticks; a goal started after the app fell silent walks on.
static void theDeadManStopLeavesAGoalAlone() {
    MotionService motion;
    standingPico(motion);
    motion.inbox.postMode(socket_message_ModesEnum_WALK);
    input(motion, sticks(0, 0.5f));
    motion.inbox.postGoal({0.2f, 0, 0, 0.5f});
    tick(motion, TICKS_PER_S); // past the 500 ms the stop waits
    CHECK(motion.linkLost());
    CHECK(motion.goalStatus().state == GoalState::RUNNING);
    CHECK(motion.currentCommand().ly > 0);
    CHECK(runGoal(motion) == GoalState::REACHED);
}

// Asked from REST, or deactivated before the control task took it: it ends at once and moves nothing.
static void aGoalTheModeNoLongerAllowsIsCancelled() {
    MotionService motion;
    standingPico(motion);
    motion.inbox.postMode(socket_message_ModesEnum_REST);
    tick(motion, TICKS_PER_S);
    motion.inbox.postGoal({0.2f, 0, 0, 0.5f});
    tick(motion);
    CHECK(motion.goalStatus().state == GoalState::CANCELLED);
    CHECK(motion.mode() == socket_message_ModesEnum_REST);
    CHECK(centred(motion));
}

// A reset of the odometry from the socket's task, mid-goal, must not look like a jump back to the start.
static void anOdometryResetMidGoalChangesNothing() {
    MotionService motion;
    standingPico(motion);
    motion.inbox.postGoal({0.2f, 0, 0, 0.5f});
    tick(motion, TICKS_PER_S);
    const OdometryEstimate before = motion.resetOdometry();
    CHECK(before.x > 0.03f);
    CHECK(runGoal(motion) == GoalState::REACHED);
    CHECK_NEAR(before.x + motion.odometry().x, 0.2f, 0.01);
}

// A new goal replaces a running one, under its own number.
static void aNewGoalReplacesTheRunningOne() {
    MotionService motion;
    standingPico(motion);
    const uint32_t first = motion.inbox.postGoal({0.3f, 0, 0, 0.5f});
    tick(motion, 50);
    const uint32_t second = motion.inbox.postGoal({0, 0, 20 * DEG, 0.5f});
    tick(motion);
    CHECK(second == first + 1);
    CHECK(motion.goalStatus().id == second);
    CHECK(motion.goalStatus().state == GoalState::RUNNING);
    CHECK(motion.currentCommand().ly == 0 && motion.currentCommand().rx > 0);
    CHECK(runGoal(motion) == GoalState::REACHED);
}

static void gesture(MotionService &motion, gesture_t which) {
    fake_timer::nowUs += 10000;
    motion.update(ImuSample {}, which);
}

// A forward gesture walks GESTURE_STEP_M and stands again; another arriving while it walks is dropped, not queued.
static void aForwardGestureStepsOnceAndIgnoresAnotherMeanwhile() {
    MotionService motion;
    standingPico(motion);
    motion.resetOdometry();
    gesture(motion, eGestureForward);
    tick(motion);
    CHECK(motion.goalStatus().state == GoalState::RUNNING);
    tick(motion, TICKS_PER_S / 2);
    gesture(motion, eGestureForward);
    gesture(motion, eGestureDown);
    CHECK(runGoal(motion) == GoalState::REACHED);
    tick(motion, TICKS_PER_S);
    CHECK(motion.mode() == socket_message_ModesEnum_STAND);
    CHECK_NEAR(motion.odometry().x, GESTURE_STEP_M, SETTLED_M);
    CHECK(motion.goalStatus().id == 1);
}

// A clockwise circle turns the robot a full turn the other way round from an anticlockwise one: to the right, on a
// negative rx.
static void aClockwiseGestureTurnsAFullCircle() {
    MotionService motion;
    standingPico(motion);
    motion.resetOdometry();
    gesture(motion, eGestureClockwise);
    tick(motion);
    CHECK(motion.goalStatus().goal.turn == -GESTURE_TURN_RAD);
    CHECK(motion.currentCommand().rx < 0);
    CHECK(runGoal(motion, 60) == GoalState::REACHED);
    tick(motion, TICKS_PER_S);
    CHECK_NEAR(std::remainder(motion.odometry().yaw, 2 * static_cast<float>(M_PI)), 0, SETTLED_RAD);
}

// A turn closes on the fused heading: with a gyro that reads 15 % more turn than the feet show, as the body turns
// when its feet undercount, the goal stops at 90 degrees by the gyro, about 78 by the feet.
static void aTurnClosesOnTheFusedHeading() {
    MotionService motion;
    standingPico(motion);
    ImuSample imu;
    imu.valid = ImuValid::ACCEL | ImuValid::GYRO;
    auto gyroTick = [&]() {
        const float feetBefore = motion.odometry().kinematicYaw;
        fake_timer::nowUs += 10000;
        imu.t_us = fake_timer::nowUs;
        imu.gyroYaw = std::remainder(imu.gyroYaw + imu.gyro[2] / TICKS_PER_S, 2 * static_cast<float>(M_PI));
        motion.update(imu, eGestureNone);
        // The next sample reads this tick's turn; a tick late is a gyro's 10 ms of lag.
        imu.gyro[2] = 1.15f * std::remainder(motion.odometry().kinematicYaw - feetBefore, 2 * M_PI) * TICKS_PER_S;
    };
    for (int i = 0; i < TICKS_PER_S; i++) gyroTick();
    motion.inbox.postGoal({0, 0, 90 * DEG, 0.5f});
    for (int i = 0; i < 30 * TICKS_PER_S && motion.goalStatus().state != GoalState::REACHED; i++) gyroTick();
    CHECK(motion.goalStatus().state == GoalState::REACHED);
    const FusedEstimate at = motion.odometry();
    CHECK_NEAR(at.yaw, 90 * DEG, 3 * DEG);
    CHECK_NEAR(at.kinematicYaw, 90 * DEG / 1.15f, 3 * DEG);
}

// A goal slows its stick near the target, into the crawl's band: under AUTO it keeps the trot it started in, rather
// than stopping to swap, and AUTO picks again once it has ended.
static void aGoalKeepsTheGaitAutoHadWhenItStarted() {
    MotionService motion;
    standingPico(motion);
    motion.inbox.postGait(socket_message_WalkGaits_AUTO);
    tick(motion);
    CHECK(motion.gait() == socket_message_WalkGaits_AUTO);
    CHECK(motion.walkedGait() == socket_message_WalkGaits_TROT);
    motion.inbox.postGoal({0.1f, 0, 0, 0.6f});
    tick(motion);
    CHECK(motion.goalStatus().state == GoalState::RUNNING);
    bool crawled = false;
    for (int i = 0; i < 30 * TICKS_PER_S && motion.goalStatus().state == GoalState::RUNNING; i++) {
        tick(motion);
        crawled |= motion.walkedGait() == socket_message_WalkGaits_CRAWL;
    }
    CHECK(motion.goalStatus().state == GoalState::REACHED);
    CHECK(!crawled);
    motion.inbox.postMode(socket_message_ModesEnum_WALK);
    tick(motion);
    for (int i = 0; i < 20; i++) {
        input(motion, sticks(0, 0.1f));
        tick(motion, TICKS_PER_S / 10);
    }
    CHECK(motion.walkedGait() == socket_message_WalkGaits_CRAWL);
}

// A walk that drifts in heading, as a trot does on the floor, which the gyro sees and the feet do not: the goal holds
// the heading it started on and its start line, where it used to curve off them (by 26 cm over a metre at 0.03 rad/s)
// and report the target reached.
static void aStraightGoalHoldsItsLineOnADriftingHeading() {
    for (const Goal &target : {Goal {1.0f, 0, 0, 0.5f}, Goal {0, 0.3f, 0, 1}}) {
        MotionService motion;
        standingPico(motion);
        ImuSample imu;
        imu.valid = ImuValid::ACCEL | ImuValid::GYRO;
        const auto gyroTick = [&]() {
            const float feetBefore = motion.odometry().kinematicYaw;
            fake_timer::nowUs += 10000;
            imu.t_us = fake_timer::nowUs;
            imu.gyroYaw = std::remainder(imu.gyroYaw + imu.gyro[2] / TICKS_PER_S, 2 * static_cast<float>(M_PI));
            motion.update(imu, eGestureNone);
            const float feetRate = std::remainder(motion.odometry().kinematicYaw - feetBefore, 2 * M_PI) * TICKS_PER_S;
            imu.gyro[2] = feetRate + (motion.mode() == socket_message_ModesEnum_WALK ? 0.03f : 0);
        };
        for (int i = 0; i < TICKS_PER_S; i++) gyroTick();
        motion.resetOdometry();
        motion.inbox.postGoal(target);
        gyroTick();
        for (int i = 0; i < 40 * TICKS_PER_S && motion.goalStatus().state == GoalState::RUNNING; i++) gyroTick();
        CHECK(motion.goalStatus().state == GoalState::REACHED);
        const FusedEstimate at = motion.odometry();
        const bool forward = target.forward != 0;
        CHECK_NEAR(forward ? at.x : at.y, forward ? target.forward : target.sideways, GoalController::REACHED_M);
        CHECK_NEAR(forward ? at.y : at.x, 0, GoalController::CROSS_REACHED_M);
        CHECK_NEAR(at.yaw, 0, 3 * DEG);
    }
}

// On a clock that moves on with every read, each part update() times lies inside the whole, a deactivated robot times
// none, and a goal is timed only while one runs.
static void anUpdateTimesItsPartsAndAGoalOnlyWhileOneRuns() {
    MotionService motion;
    motion.useConfig(&PICO);
    motion.begin();
    fake_timer::source = [] { return fake_timer::nowUs + 1; };
    motion.update(ImuSample {}, eGestureNone);
    const MotionService::Timing deactivated = motion.timing();
    fake_timer::source = nullptr;
    CHECK(deactivated.totalUs > 0);
    CHECK(deactivated.stepUs == 0 && deactivated.odometryUs == 0 && deactivated.kinematicsUs == 0);

    motion.inbox.postMode(socket_message_ModesEnum_STAND);
    tick(motion, TICKS_PER_S);
    fake_timer::source = [] { return fake_timer::nowUs + 1; };
    motion.update(ImuSample {}, eGestureNone);
    const MotionService::Timing standing = motion.timing();
    motion.inbox.postGoal({0.3f, 0, 0, 0.5f});
    motion.update(ImuSample {}, eGestureNone);
    const MotionService::Timing driving = motion.timing();
    fake_timer::source = nullptr;
    CHECK(motion.goalStatus().state == GoalState::RUNNING);
    CHECK(standing.goalUs == 0);
    CHECK(driving.goalUs > 0);
    for (const MotionService::Timing &timing : {standing, driving}) {
        CHECK(timing.stepUs > 0 && timing.odometryUs > 0 && timing.kinematicsUs > 0);
        CHECK(timing.stepUs + timing.odometryUs + timing.goalUs + timing.kinematicsUs < timing.totalUs);
    }
}

int main() {
    anUpdateTimesItsPartsAndAGoalOnlyWhileOneRuns();
    aForwardGestureStepsOnceAndIgnoresAnotherMeanwhile();
    aClockwiseGestureTurnsAFullCircle();
    aGoalWalksTheOdometryToItsTarget();
    aRobotNoAppHasDrivenStillWalks();
    aStickOffCentreTakesOverAndACentredOneDoesNot();
    aCancelAModeOrAGaitEndsIt();
    itEndsWhenTheClientThatStartedItLeaves();
    itEndsWhenTheClientThatStartedItFallsSilent();
    theDeadManStopLeavesAGoalAlone();
    aGoalTheModeNoLongerAllowsIsCancelled();
    anOdometryResetMidGoalChangesNothing();
    aNewGoalReplacesTheRunningOne();
    aTurnClosesOnTheFusedHeading();
    aStraightGoalHoldsItsLineOnADriftingHeading();
    aGoalKeepsTheGaitAutoHadWhenItStarted();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
