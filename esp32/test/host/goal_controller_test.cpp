// Host test of goal_controller.h, built and run by test_host_programs.py.
#include <cmath>
#include <cstdio>
#include <goal_controller.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                                         \
    do {                                                                                                \
        const double a_ = (actual), e_ = (expected);                                                    \
        if (!(std::fabs(a_ - e_) <= (tolerance))) {                                                     \
            std::printf("FAIL %s:%d: %s = %.6f, expected %.6f\n", __FILE__, __LINE__, #actual, a_, e_); \
            failures++;                                                                                 \
        }                                                                                               \
    } while (0)

constexpr float DT = 0.01f;
constexpr float TOP_SPEED = 0.128f; // the Pico's trot at a full stick, m/s
constexpr float TOP_TURN = 1.33f;   // rad/s
constexpr float DEG = static_cast<float>(M_PI) / 180.0f;
const BodyStep STILL {};

/**
 * A body that follows the sticks through a first-order lag, as the gait's command filter does, at `scale` of what they
 * ask, and drifts in heading by `drift` rad/s per full stick it walks. It predicts `predicts` of the coast its lag
 * still carries it on by, and stands still once the lag has all but died away.
 */
struct Body {
    float scale = 1, lag = 0.1f, drift = 0, predicts = 1;
    GoalSticks moving;

    BodyStep step(const GoalSticks &sticks) {
        const float k = DT / (lag + DT);
        moving.forward += k * (sticks.forward - moving.forward);
        moving.left += k * (sticks.left - moving.left);
        moving.turn += k * (sticks.turn - moving.turn);
        const float walked = std::fabs(moving.forward) + std::fabs(moving.left);
        return {moving.forward * TOP_SPEED * scale * DT, moving.left * TOP_SPEED * scale * DT,
                (moving.turn * TOP_TURN * scale + drift * walked) * DT};
    }

    // What the lag carries the body on by once the sticks centre: its speed now times the lag, in its own frame.
    BodyStep toRest() const {
        const float coast = predicts * lag * scale;
        return {moving.forward * TOP_SPEED * coast, moving.left * TOP_SPEED * coast, moving.turn * TOP_TURN * coast};
    }

    bool atRest() const {
        return std::fabs(moving.forward) < 1e-3f && std::fabs(moving.left) < 1e-3f && std::fabs(moving.turn) < 1e-3f;
    }
};

struct Run {
    GoalState end;
    float seconds;
    float maxStick; // the largest deflection asked for along the target's axis
    float x = 0, y = 0, heading = 0;
};

// Walks the goal on the body to its end, and the body's pose in the frame the goal started in.
static Run drive(GoalController &goal, Body &body, int maxTicks = 100000) {
    Run run {GoalState::RUNNING, 0, 0};
    GoalSticks sticks = goal.update(STILL, body.toRest(), body.atRest(), 0);
    int ticks = 0;
    while (goal.running() && ticks++ < maxTicks) {
        const Goal &g = goal.goal();
        const float along = g.turn != 0 ? sticks.turn : g.sideways != 0 ? sticks.left : sticks.forward;
        run.maxStick = std::max(run.maxStick, std::fabs(along));
        const BodyStep step = body.step(sticks);
        run.x += std::cos(run.heading) * step.forward - std::sin(run.heading) * step.left;
        run.y += std::sin(run.heading) * step.forward + std::cos(run.heading) * step.left;
        run.heading += step.turn;
        sticks = goal.update(step, body.toRest(), body.atRest(), DT);
    }
    run.end = goal.state();
    run.seconds = ticks * DT;
    return run;
}

// The walk stands still until the goal has judged where it stands: it ends, or starts its next pass. Returns the
// sticks it asks for then.
static GoalSticks standStill(GoalController &goal) {
    const int passes = goal.passes();
    GoalSticks sticks;
    for (int i = 0; i < 1000 && goal.running() && goal.passes() == passes; i++)
        sticks = goal.update(STILL, STILL, true, DT);
    return sticks;
}

// It stops where its resting pose meets the target and ends there once the body stands still: one pass, the lag's
// coast included.
static void aForwardGoalComesToRestOnItsTarget() {
    GoalController goal;
    Body body;
    goal.start({0.2f, 0, 0, 0.5f}, TOP_SPEED, TOP_TURN);
    const Run run = drive(goal, body);
    CHECK(run.end == GoalState::REACHED);
    CHECK(goal.passes() == 1);
    CHECK_NEAR(run.x, 0.2f, 1e-3);
    CHECK_NEAR(goal.progress(), run.x, 1e-5);
    CHECK_NEAR(run.maxStick, 0.5f, 1e-6);
    // 0.15 m at full pace and the last 5 cm slower, but never below the floor, and the stop: between the bounds.
    const float pace = 0.5f * TOP_SPEED;
    CHECK(run.seconds > 0.2f / pace);
    CHECK(run.seconds < 0.15f / pace + 0.05f / (GoalController::MIN_FRACTION * pace) + 1);
    const GoalSticks after = goal.update({0.1f, 0, 0}, STILL, false, DT); // an ended goal asks for nothing more
    CHECK(after.forward == 0 && after.left == 0 && after.turn == 0);
}

// Full speed until the last 5 cm or 30 degrees, in proportion over them, and never below a fifth of the asked speed.
static void itSlowsOverTheLastStretchDownToAFloor() {
    GoalController goal;
    goal.start({0.2f, 0, 0, 0.8f}, TOP_SPEED, TOP_TURN);
    CHECK_NEAR(goal.update({0.1f, 0, 0}, STILL, false, DT).forward, 0.8f, 1e-6);   // 10 cm left
    CHECK_NEAR(goal.update({0.07f, 0, 0}, STILL, false, DT).forward, 0.48f, 1e-5); // 3 cm left: 3/5 of the speed
    CHECK_NEAR(goal.update({0.018f, 0, 0}, STILL, false, DT).forward, 0.192f, 1e-5);
    CHECK_NEAR(goal.update({0.004f, 0, 0}, STILL, false, DT).forward, 0.16f, 1e-5); // 8 mm left: the floor

    goal.start({0, 0, 90 * DEG, 0.8f}, TOP_SPEED, TOP_TURN);
    CHECK_NEAR(goal.update({0, 0, 60 * DEG}, STILL, false, DT).turn, 0.8f, 1e-6);
    CHECK_NEAR(goal.update({0, 0, 15 * DEG}, STILL, false, DT).turn, 0.4f, 1e-5);
    CHECK_NEAR(goal.update({0, 0, 11 * DEG}, STILL, false, DT).turn, 0.16f, 1e-5); // 4 degrees left: the floor
}

// A walk fast enough that two stops' coasts outrun SLOW_DOWN slows over those instead.
static void aFastGoalSlowsOverWhatItsStopNeeds() {
    constexpr float FAST = 0.4f;
    const float zone = 2 * FAST * GoalController::STOP_S;
    CHECK(zone > GoalController::SLOW_DOWN_M);
    GoalController goal;
    goal.start({1, 0, 0, 1}, FAST, TOP_TURN);
    CHECK_NEAR(goal.update({1 - zone, 0, 0}, STILL, false, DT).forward, 1, 1e-4);
    CHECK_NEAR(goal.update({zone / 2, 0, 0}, STILL, false, DT).forward, 0.5f, 1e-3);
}

// The stop carries the body on: once where it would come to rest reaches the target, it centres its sticks short of
// it, and judges where the robot stands only once the walk stands still.
static void itStopsWhereItsRestingPoseMeetsTheTarget() {
    GoalController goal;
    goal.start({0.2f, 0, 0, 0.5f}, TOP_SPEED, TOP_TURN);
    CHECK(goal.update({0.17f, 0, 0}, {0.02f, 0, 0}, false, DT).forward > 0);
    const GoalSticks centred = goal.update({0.005f, 0, 0}, {0.026f, 0, 0}, false, DT);
    CHECK(centred.forward == 0 && centred.left == 0 && centred.turn == 0);
    CHECK(goal.running());
    goal.update({0.02f, 0, 0}, {0.006f, 0, 0}, false, DT);
    CHECK(goal.running());
    goal.update({0.006f, 0, 0}, STILL, true, DT);
    CHECK(goal.running());
    standStill(goal);
    CHECK(goal.state() == GoalState::REACHED);
    CHECK_NEAR(goal.progress(), 0.201f, 1e-5);
}

// The body coasts on, and the servos lag, once the walk's feet stop: the pose is judged once the walk has stood still
// for STILL_S, from the start again whenever it moves.
static void itJudgesOnlyOnceTheWalkHasStoodStillAWhile() {
    GoalController goal;
    goal.start({0.2f, 0, 0, 0.5f}, TOP_SPEED, TOP_TURN);
    goal.update({0.2f, 0, 0}, STILL, false, DT);
    const int ticks = static_cast<int>(GoalController::STILL_S / DT);
    for (int i = 0; i < ticks - 2; i++) goal.update(STILL, STILL, true, DT);
    goal.update(STILL, STILL, false, DT);
    for (int i = 0; i < ticks - 2; i++) goal.update(STILL, STILL, true, DT);
    CHECK(goal.running());
    for (int i = 0; i < 3; i++) goal.update(STILL, STILL, true, DT);
    CHECK(goal.state() == GoalState::REACHED);
}

// Where the robot came to rest beyond the tolerance, another pass walks the rest, back if it overshot.
static void anOvershootGetsACorrectivePass() {
    GoalController goal;
    goal.start({0.2f, 0, 0, 0.5f}, TOP_SPEED, TOP_TURN);
    goal.update({0.2f, 0, 0}, {0.03f, 0, 0}, false, DT);
    goal.update({0.03f, 0, 0}, STILL, false, DT);
    const GoalSticks back = standStill(goal);
    CHECK(goal.running() && goal.passes() == 2);
    CHECK_NEAR(back.forward, -0.5f * 0.6f, 1e-5); // 3 cm to walk back: 3/5 of the speed
}

// A body that coasts on far beyond what it says overshoots every pass: the third that does not land times out. One
// that undercounts its coast less lands on a later pass.
static void itGivesUpAfterThreePasses() {
    GoalController goal;
    Body body;
    body.predicts = 0;
    body.lag = 0.25f; // at the floor it coasts on 3.8 degrees
    goal.start({0, 0, 20 * DEG, 1}, TOP_SPEED, TOP_TURN);
    CHECK(drive(goal, body).end == GoalState::TIMED_OUT);
    CHECK(goal.passes() == GoalController::MAX_PASSES);

    GoalController second;
    Body undercounting;
    undercounting.predicts = 0.2f;
    undercounting.lag = 0.25f;
    second.start({0, 0, 20 * DEG, 1}, TOP_SPEED, TOP_TURN);
    const Run landed = drive(second, undercounting);
    CHECK(landed.end == GoalState::REACHED);
    CHECK(second.passes() > 1);
    CHECK_NEAR(landed.heading, 20 * DEG, GoalController::REACHED_RAD);
}

// A body that drifts in heading walks a curve: the goal holds the heading it started on and its start line, and the
// target still lies along the axis it faced when it started.
static void aStraightGoalHoldsItsHeadingAndItsLine() {
    for (const Goal &target : {Goal {0.3f, 0, 0, 1}, Goal {0, -0.2f, 0, 1}}) {
        GoalController goal;
        Body body;
        body.drift = 0.2f; // 11 degrees a second at a full stick
        goal.start(target, TOP_SPEED, TOP_TURN);
        const Run run = drive(goal, body);
        CHECK(run.end == GoalState::REACHED);
        CHECK(std::fabs(run.heading) < 0.1f);
        const bool forward = target.forward != 0;
        CHECK_NEAR(run.x, target.forward, forward ? GoalController::REACHED_M : GoalController::CROSS_REACHED_M);
        CHECK_NEAR(run.y, target.sideways, forward ? GoalController::CROSS_REACHED_M : GoalController::REACHED_M);
        CHECK_NEAR(goal.progress(), forward ? run.x : run.y, 1e-5);
    }
}

// Arriving off its line, it walks only across before it stops; come to rest on the target but off the line, a pass
// walks only across: no stick along the axis.
static void onTheTargetButOffTheLineItWalksOnlyAcross() {
    GoalController goal;
    goal.start({0.2f, 0, 0, 0.5f}, TOP_SPEED, TOP_TURN);
    const GoalSticks arriving = goal.update({0.2f, 0.03f, 0}, STILL, false, DT);
    CHECK(arriving.forward == 0 && arriving.left < 0);
    goal.update({0, -0.015f, 0}, STILL, false, DT);
    goal.update({0, 0.015f, 0}, STILL, true, DT);
    const GoalSticks across = standStill(goal);
    CHECK(goal.passes() == 2);
    CHECK(across.forward == 0 && across.left < 0);
    goal.update({0, -0.015f, 0}, STILL, false, DT);
    CHECK(goal.running());
    standStill(goal);
    CHECK(goal.state() == GoalState::REACHED);
}

// However short, a first pass walks its target: one no longer than the tolerance is not already there.
static void aGoalWithinItsToleranceStillWalks() {
    GoalController goal;
    goal.start({0.01f, 0, 0, 1}, TOP_SPEED, TOP_TURN);
    CHECK_NEAR(goal.update(STILL, STILL, true, 0).forward, GoalController::MIN_FRACTION, 1e-6);
}

static void sidewaysAndTurnsInBothDirections() {
    {
        GoalController goal;
        Body body;
        goal.start({0, -0.15f, 0, 0.5f}, TOP_SPEED, TOP_TURN);
        const Run run = drive(goal, body);
        CHECK(run.end == GoalState::REACHED);
        CHECK_NEAR(run.y, -0.15f, GoalController::REACHED_M);
    }
    {
        GoalController goal;
        Body body;
        body.scale = 0.9f; // a gait that turns slower than its sticks say still lands
        goal.start({0, 0, 90 * DEG, 0.5f}, TOP_SPEED, TOP_TURN);
        const Run run = drive(goal, body);
        CHECK(run.end == GoalState::REACHED);
        CHECK_NEAR(run.heading, 90 * DEG, GoalController::REACHED_RAD);
    }
    {
        // Beyond half a turn the heading must not wrap.
        GoalController goal;
        Body body;
        goal.start({0, 0, -400 * DEG, 1}, TOP_SPEED, TOP_TURN);
        const Run run = drive(goal, body);
        CHECK(run.end == GoalState::REACHED);
        CHECK_NEAR(run.heading, -400 * DEG, GoalController::REACHED_RAD);
    }
}

// Twice the expected time plus 2 s: a robot that cannot move gives up, and centres its sticks.
static void aGoalThatGetsNowhereTimesOut() {
    GoalController goal;
    Body body;
    body.scale = 0;
    goal.start({0.2f, 0, 0, 0.5f}, TOP_SPEED, TOP_TURN);
    const float pace = 0.5f * TOP_SPEED;
    const float expected = 0.15f / pace + 0.05f / (GoalController::MIN_FRACTION * pace);
    CHECK_NEAR(goal.timeout(), 2 * expected + 2, 1e-4);
    const Run run = drive(goal, body);
    CHECK(run.end == GoalState::TIMED_OUT);
    CHECK_NEAR(run.seconds, goal.timeout(), 2 * DT);

    // A short turn is all slow zone.
    goal.start({0, 0, 10 * DEG, 1}, TOP_SPEED, TOP_TURN);
    CHECK_NEAR(goal.timeout(), 2 * (10 * DEG / (GoalController::MIN_FRACTION * TOP_TURN)) + 2, 1e-4);

    // Each pass has its own time: the goal may take what the passes before took and what the one under way may.
    goal.start({0.2f, 0, 0, 0.5f}, TOP_SPEED, TOP_TURN);
    for (int i = 0; i < 300; i++) goal.update(i == 0 ? BodyStep {0.23f, 0, 0} : STILL, STILL, false, DT);
    int still = 0;
    for (; goal.passes() == 1; still++) goal.update(STILL, STILL, true, DT);
    CHECK(goal.passes() == 2);
    CHECK_NEAR(goal.timeout(), (300 + still) * DT + 2 * (0.03f / (GoalController::MIN_FRACTION * pace)) + 2, 1e-3);
}

static void aCancelEndsOnlyARunningGoal() {
    GoalController goal;
    goal.cancel();
    CHECK(goal.state() == GoalState::IDLE);
    goal.start({0, 0.1f, 0, 0.5f}, TOP_SPEED, TOP_TURN);
    CHECK(goal.update(STILL, STILL, true, DT).left == 0.5f);
    goal.cancel();
    CHECK(goal.state() == GoalState::CANCELLED);
    const GoalSticks after = goal.update({0, 0.01f, 0}, STILL, false, DT);
    CHECK(after.forward == 0 && after.left == 0 && after.turn == 0);
    CHECK(goal.progress() == 0);

    goal.start({0, 0, 0.1f, 0.5f}, TOP_SPEED, TOP_TURN);
    goal.update({0, 0, 0.1f}, STILL, true, DT);
    standStill(goal);
    CHECK(goal.state() == GoalState::REACHED);
    goal.cancel();
    CHECK(goal.state() == GoalState::REACHED);
}

static void onlyOneFiniteTargetAtASaneSpeedIsValid() {
    CHECK(GoalController::valid({0.2f, 0, 0, 0.5f}));
    CHECK(GoalController::valid({0, -0.1f, 0, 1}));
    CHECK(GoalController::valid({0, 0, 3, 0.01f}));
    CHECK(!GoalController::valid({0, 0, 0, 0.5f}));
    CHECK(!GoalController::valid({0.2f, 0.1f, 0, 0.5f}));
    CHECK(!GoalController::valid({0.2f, 0, 1, 0.5f}));
    CHECK(!GoalController::valid({NAN, 0, 0, 0.5f}));
    CHECK(!GoalController::valid({INFINITY, 0, 0, 0.5f}));
    CHECK(!GoalController::valid({0.2f, 0, 0, 0}));
    CHECK(!GoalController::valid({0.2f, 0, 0, 1.01f}));
    CHECK(!GoalController::valid({0.2f, 0, 0, NAN}));
}

int main() {
    aForwardGoalComesToRestOnItsTarget();
    itSlowsOverTheLastStretchDownToAFloor();
    aFastGoalSlowsOverWhatItsStopNeeds();
    itStopsWhereItsRestingPoseMeetsTheTarget();
    itJudgesOnlyOnceTheWalkHasStoodStillAWhile();
    anOvershootGetsACorrectivePass();
    itGivesUpAfterThreePasses();
    aStraightGoalHoldsItsHeadingAndItsLine();
    onTheTargetButOffTheLineItWalksOnlyAcross();
    aGoalWithinItsToleranceStillWalks();
    sidewaysAndTurnsInBothDirections();
    aGoalThatGetsNowhereTimesOut();
    aCancelEndsOnlyARunningGoal();
    onlyOneFiniteTargetAtASaneSpeedIsValid();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
