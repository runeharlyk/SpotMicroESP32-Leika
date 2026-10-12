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

#define CHECK_NEAR(actual, expected, tolerance)                                                                  \
    do {                                                                                                         \
        const double a_ = (actual), e_ = (expected);                                                             \
        if (!(std::fabs(a_ - e_) <= (tolerance))) {                                                              \
            std::printf("FAIL %s:%d: %s = %.6f, expected %.6f\n", __FILE__, __LINE__, #actual, a_, e_);          \
            failures++;                                                                                          \
        }                                                                                                        \
    } while (0)

constexpr float DT = 0.01f;
constexpr float TOP_SPEED = 0.128f; // the Pico's trot at a full stick, m/s
constexpr float TOP_TURN = 1.33f;   // rad/s
constexpr float DEG = static_cast<float>(M_PI) / 180.0f;

/**
 * A body that follows the sticks through a first-order lag, as the gait's command filter does, at `scale` of what they
 * ask, while drifting `drift` rad/s in heading.
 */
struct Body {
    float scale = 1, lag = 0.1f, drift = 0;
    GoalSticks moving;

    BodyStep step(const GoalSticks &sticks) {
        const float k = DT / (lag + DT);
        moving.forward += k * (sticks.forward - moving.forward);
        moving.left += k * (sticks.left - moving.left);
        moving.turn += k * (sticks.turn - moving.turn);
        return {moving.forward * TOP_SPEED * scale * DT, moving.left * TOP_SPEED * scale * DT,
                (moving.turn * TOP_TURN * scale + drift) * DT};
    }
};

struct Run {
    GoalState end;
    float seconds;
    float progress;    // when it ended
    float maxStick;    // the largest deflection asked for
    float minStick;    // the smallest one while running
    float overrun = 0; // what the lag carried it on once the sticks centred, along the axis
};

static Run drive(GoalController &goal, Body &body, int maxTicks = 100000) {
    Run run {GoalState::RUNNING, 0, 0, 0, 1};
    GoalSticks sticks = goal.update({}, 0);
    int ticks = 0;
    while (goal.running() && ticks++ < maxTicks) {
        const float deflection = std::fabs(sticks.forward + sticks.left + sticks.turn);
        run.maxStick = std::max(run.maxStick, deflection);
        run.minStick = std::min(run.minStick, deflection);
        sticks = goal.update(body.step(sticks), DT);
    }
    run.end = goal.state();
    run.seconds = ticks * DT;
    run.progress = goal.progress();
    for (int i = 0; i < 100; i++) {
        const BodyStep step = body.step({});
        run.overrun += goal.goal().turn != 0 ? step.turn : goal.goal().sideways != 0 ? step.left : step.forward;
    }
    return run;
}

static void aForwardGoalStopsWithinItsTolerance() {
    GoalController goal;
    Body body;
    goal.start({0.2f, 0, 0, 0.5f}, TOP_SPEED, TOP_TURN);
    const Run run = drive(goal, body);
    CHECK(run.end == GoalState::REACHED);
    CHECK_NEAR(run.progress, 0.2f, GoalController::REACHED_M);
    CHECK_NEAR(run.maxStick, 0.5f, 1e-6);
    // The lag carries it on by about the floor speed times the lag: 0.2 x 0.5 x 0.128 m/s x 0.1 s.
    CHECK(run.overrun > 0 && run.overrun < 0.002f);
    CHECK_NEAR(run.progress + run.overrun, 0.2f, 0.003);
    // 0.15 m at full pace and the last 5 cm slower, but never below the floor: between the two bounds.
    const float pace = 0.5f * TOP_SPEED;
    CHECK(run.seconds > 0.2f / pace);
    CHECK(run.seconds < 0.15f / pace + 0.05f / (GoalController::MIN_FRACTION * pace));
    CHECK(goal.update({0.1f, 0, 0}, DT).forward == 0); // an ended goal asks for nothing more
}

// Full speed until the last 5 cm or 30 degrees, in proportion over them, and never below a fifth of the asked speed.
// It walks on, at the floor, until it is past the target.
static void itSlowsOverTheLastStretchDownToAFloor() {
    GoalController goal;
    goal.start({0.2f, 0, 0, 0.8f}, TOP_SPEED, TOP_TURN);
    CHECK_NEAR(goal.update({0.1f, 0, 0}, DT).forward, 0.8f, 1e-6);   // 10 cm left
    CHECK_NEAR(goal.update({0.07f, 0, 0}, DT).forward, 0.48f, 1e-5); // 3 cm left: 3/5 of the speed
    CHECK_NEAR(goal.update({0.018f, 0, 0}, DT).forward, 0.192f, 1e-5);
    CHECK_NEAR(goal.update({0.004f, 0, 0}, DT).forward, 0.16f, 1e-5); // 8 mm left, within the tolerance: the floor
    CHECK(goal.update({0.01f, 0, 0}, DT).forward == 0);               // 2 mm past
    CHECK(goal.state() == GoalState::REACHED);

    goal.start({0, 0, 90 * DEG, 0.8f}, TOP_SPEED, TOP_TURN);
    CHECK_NEAR(goal.update({0, 0, 60 * DEG}, DT).turn, 0.8f, 1e-6);
    CHECK_NEAR(goal.update({0, 0, 15 * DEG}, DT).turn, 0.4f, 1e-5);
    CHECK_NEAR(goal.update({0, 0, 11 * DEG}, DT).turn, 0.16f, 1e-5); // 4 degrees left: the floor
    CHECK_NEAR(goal.update({0, 0, 2 * DEG}, DT).turn, 0.16f, 1e-5);
    CHECK(goal.update({0, 0, 3 * DEG}, DT).turn == 0);
    CHECK(goal.state() == GoalState::REACHED);
}

// Past the target by more than the tolerance it walks back.
static void anOvershootIsWalkedBack() {
    GoalController goal;
    goal.start({0.1f, 0, 0, 0.5f}, TOP_SPEED, TOP_TURN);
    CHECK_NEAR(goal.update({0.13f, 0, 0}, DT).forward, -0.5f * 0.6f, 1e-5);
}

// The target lies along the axis it faced when it started: a heading picked up on the way does not move the target.
static void theTargetStaysInTheFrameItStartedIn() {
    GoalController goal;
    Body body;
    body.drift = 0.2f; // 11 degrees a second
    goal.start({0.1f, 0, 0, 1}, TOP_SPEED, TOP_TURN);
    GoalSticks sticks = goal.update({}, 0);
    float x = 0, heading = 0;
    while (goal.running()) {
        const BodyStep step = body.step(sticks);
        x += std::cos(heading) * step.forward - std::sin(heading) * step.left;
        heading += step.turn;
        sticks = goal.update(step, DT);
    }
    CHECK(goal.state() == GoalState::REACHED);
    CHECK(heading > 0.1f);
    CHECK_NEAR(goal.progress(), x, 1e-5);
    CHECK_NEAR(x, 0.1f, GoalController::REACHED_M);
}

static void sidewaysAndTurnsInBothDirections() {
    {
        GoalController goal;
        Body body;
        goal.start({0, -0.15f, 0, 0.5f}, TOP_SPEED, TOP_TURN);
        const Run run = drive(goal, body);
        CHECK(run.end == GoalState::REACHED);
        CHECK_NEAR(run.progress, -0.15f, GoalController::REACHED_M);
    }
    {
        GoalController goal;
        Body body;
        body.scale = 0.9f; // a gait that turns slower than its sticks say still lands
        goal.start({0, 0, 90 * DEG, 0.5f}, TOP_SPEED, TOP_TURN);
        const Run run = drive(goal, body);
        CHECK(run.end == GoalState::REACHED);
        CHECK_NEAR(run.progress, 90 * DEG, GoalController::REACHED_RAD);
        CHECK_NEAR(run.progress + run.overrun, 90 * DEG, GoalController::REACHED_RAD);
        CHECK(run.minStick >= GoalController::MIN_FRACTION * 0.5f - 1e-6f);
    }
    {
        // Beyond half a turn the heading must not wrap.
        GoalController goal;
        Body body;
        goal.start({0, 0, -400 * DEG, 1}, TOP_SPEED, TOP_TURN);
        const Run run = drive(goal, body);
        CHECK(run.end == GoalState::REACHED);
        CHECK_NEAR(run.progress, -400 * DEG, GoalController::REACHED_RAD);
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

    // One that stalls within the tolerance short of the target has arrived when its time is up.
    goal.start({0.2f, 0, 0, 0.5f}, TOP_SPEED, TOP_TURN);
    goal.update({0.195f, 0, 0}, DT);
    CHECK(goal.running());
    for (int i = 0; i < 2 * goal.timeout() / DT && goal.running(); i++) goal.update({}, DT);
    CHECK(goal.state() == GoalState::REACHED);

    // A short turn is all slow zone.
    goal.start({0, 0, 10 * DEG, 1}, TOP_SPEED, TOP_TURN);
    CHECK_NEAR(goal.timeout(), 2 * (10 * DEG / (GoalController::MIN_FRACTION * TOP_TURN)) + 2, 1e-4);
}

static void aCancelEndsOnlyARunningGoal() {
    GoalController goal;
    goal.cancel();
    CHECK(goal.state() == GoalState::IDLE);
    goal.start({0, 0.1f, 0, 0.5f}, TOP_SPEED, TOP_TURN);
    CHECK(goal.update({}, DT).left == 0.5f);
    goal.cancel();
    CHECK(goal.state() == GoalState::CANCELLED);
    const GoalSticks after = goal.update({0, 0.01f, 0}, DT);
    CHECK(after.forward == 0 && after.left == 0 && after.turn == 0);
    CHECK(goal.progress() == 0);

    goal.start({0, 0, 0.1f, 0.5f}, TOP_SPEED, TOP_TURN);
    goal.update({0, 0, 0.1f}, DT);
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
    aForwardGoalStopsWithinItsTolerance();
    itSlowsOverTheLastStretchDownToAFloor();
    anOvershootIsWalkedBack();
    theTargetStaysInTheFrameItStartedIn();
    sidewaysAndTurnsInBothDirections();
    aGoalThatGetsNowhereTimesOut();
    aCancelEndsOnlyARunningGoal();
    onlyOneFiniteTargetAtASaneSpeedIsValid();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
