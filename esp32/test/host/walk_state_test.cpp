// Host test of motion_states/walk_state.h, built and run by test_host_programs.py.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <variant.h>
// The test reads the gait phase to know which feet are on the ground.
#define private public
#include <motion_states/walk_state.h>
#undef private

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

constexpr float DT = 0.01f;

struct StanceHeights {
    float highest = -INFINITY;     // the highest a stance foot stood above its standing height, m
    float deepestLiftOff = 0.0f;   // the furthest from standing height a foot left the ground at, m
};

// Walks steadily on `lx`, `ly` and watches each foot's height while the gait has it on the ground.
static StanceHeights stanceHeights(const KinConfig &config, float lx, float ly) {
    WalkState walk;
    walk.configure(config);
    MotionState &state = walk;
    body_state_t body;
    body.ym = config.default_body_height;
    body.updateFeet(config.default_feet_positions);
    state.begin();
    state.handleCommand({lx, ly, 0, 0, 0.5f, 0.5f, 0.5f});
    for (int i = 0; i < 300; i++) state.step(body, DT);

    StanceHeights heights;
    bool wasStance[4] = {false, false, false, false};
    float lastStanceHeight[4] = {0, 0, 0, 0};
    for (int i = 0; i < 400; i++) {
        state.step(body, DT);
        for (int leg = 0; leg < 4; leg++) {
            const bool stance = std::fmod(walk.phase_time + walk.phase_offset[leg], 1.0f) <= walk.stand_offset;
            const float height = body.feet[leg][1] - config.default_feet_positions[leg][1];
            if (stance) {
                heights.highest = std::max(heights.highest, height);
                lastStanceHeight[leg] = height;
            } else if (wasStance[leg]) {
                heights.deepestLiftOff = std::max(heights.deepestLiftOff, std::fabs(lastStanceHeight[leg]));
            }
            wasStance[leg] = stance;
        }
    }
    return heights;
}

// The stance sinks the foot along its stroke and levels it at both ends, so lift-off meets the swing without a jump.
// Measured along x + z instead of along the stroke, one diagonal lifted the foot above its standing height at the ends
// of the stance, and the other kept it sunk to lift-off and dropped it there.
static void aStanceFootStaysDownAndLiftsOffLevelInEveryDirection() {
    const KinematicsVariant variants[] = {socket_message_KinematicsVariant_SPOTMICRO_ESP32,
                                          socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI,
                                          socket_message_KinematicsVariant_SPOTMICRO_YERTLE};
    const float d = 0.70710678f;
    const float directions[][2] = {{0, 1}, {-1, 0}, {d, d}, {-d, d}, {d, -d}, {-d, -d}};
    for (KinematicsVariant variant : variants) {
        const KinConfig &config = *kinConfigFor(variant);
        const float depth = config.default_step_depth;
        for (const auto &direction : directions) {
            const StanceHeights heights = stanceHeights(config, direction[0], direction[1]);
            const bool down = heights.highest <= 1e-6f;
            const bool level = heights.deepestLiftOff <= 0.2f * depth;
            if (!down || !level)
                std::printf("%s lx=%.2f ly=%.2f: stance up to %.2f mm above standing, lift-off %.2f mm from level\n",
                            variantName(variant), direction[0], direction[1], heights.highest * 1000.0f,
                            heights.deepestLiftOff * 1000.0f);
            CHECK(down);
            CHECK(level);
        }
    }
}

// The bands: the crawl up to its full stroke, 0.22 of the trot's top speed, the trot above, and the trot holds its band
// 0.06 further down.
static void autoPicksTheGaitBySpeedWithHysteresis() {
    CHECK(std::fabs(AUTO_CRAWL_MAX - 0.2206f) < 0.0001f);
    CHECK(selectAutoGait(0.0f, WALK_GAIT::TROT) == WALK_GAIT::CRAWL);
    CHECK(selectAutoGait(0.15f, WALK_GAIT::TROT) == WALK_GAIT::CRAWL);
    CHECK(selectAutoGait(0.17f, WALK_GAIT::TROT) == WALK_GAIT::TROT);
    CHECK(selectAutoGait(AUTO_CRAWL_MAX, WALK_GAIT::CRAWL) == WALK_GAIT::CRAWL);
    CHECK(selectAutoGait(0.23f, WALK_GAIT::CRAWL) == WALK_GAIT::TROT);
    CHECK(selectAutoGait(1.0f, WALK_GAIT::CRAWL) == WALK_GAIT::TROT);
    // Between the bands' edges the gait in force stays.
    for (const float demand : {0.17f, 0.19f, 0.22f}) {
        CHECK(selectAutoGait(demand, WALK_GAIT::CRAWL) == WALK_GAIT::CRAWL);
        CHECK(selectAutoGait(demand, WALK_GAIT::TROT) == WALK_GAIT::TROT);
    }
}

// A WalkState in AUTO on the Mini, with the per-tick foot travel and the schedule swaps it made.
struct AutoWalker {
    const KinConfig &config = *kinConfigFor(socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI);
    WalkState walk;
    body_state_t body;
    int swaps = 0, swapsWhileMoving = 0;
    float worstFootStep = 0; // m in one tick, any foot, any axis

    explicit AutoWalker(WALK_GAIT start) {
        walk.configure(config);
        body.ym = config.default_body_height;
        body.updateFeet(config.default_feet_positions);
        static_cast<MotionState &>(walk).begin();
        start == WALK_GAIT::CRAWL ? walk.set_mode_crawl() : walk.set_mode_trot();
    }

    bool standingStill() const {
        const gait_state_t &steps = walk.steps();
        const auto down = walk.stance();
        const bool still = std::fabs(steps.step_x) < 0.001f && std::fabs(steps.step_z) < 0.001f &&
                           std::fabs(steps.step_angle) < 0.001f;
        return still && down[0] && down[1] && down[2] && down[3];
    }

    // Holds the forward stick `ly` and the turn stick `rx` for `ticks` ticks.
    void hold(float ly, int ticks, float rx = 0) {
        static_cast<MotionState &>(walk).handleCommand({0, ly, rx, 0, 0.5f, 0.5f, 0.5f});
        for (int t = 0; t < ticks; t++) {
            const WALK_GAIT before = walk.gait();
            const bool still = standingStill();
            float feet[4][3];
            for (int leg = 0; leg < 4; leg++)
                for (int axis = 0; axis < 3; axis++) feet[leg][axis] = body.feet[leg][axis];
            static_cast<MotionState &>(walk).step(body, DT);
            for (int leg = 0; leg < 4; leg++)
                for (int axis = 0; axis < 3; axis++)
                    worstFootStep = std::max(worstFootStep, std::fabs(body.feet[leg][axis] - feet[leg][axis]));
            if (walk.gait() != before) {
                swaps++;
                if (!still) swapsWhileMoving++;
            }
        }
    }
};

// A stick pushed past the crawl's band mid-walk brings the walk to a standstill on all four feet, and it walks on in
// the trot; a swap mid-stride would reassign every leg's phase and lift a loaded foot or drop a swinging one.
static void autoSwapsTheGaitOnlyAtAStandstill() {
    AutoWalker walker(WALK_GAIT::TROT);
    walker.walk.set_auto_gait(true);
    walker.hold(0.1f, 300);
    CHECK(walker.walk.gait() == WALK_GAIT::CRAWL);
    CHECK(walker.walk.steps().step_x > 0);
    walker.hold(1.0f, 400);
    CHECK(walker.walk.gait() == WALK_GAIT::TROT);
    CHECK(walker.walk.steps().step_x > 0.9f * walker.config.max_step_length);
    walker.hold(0.1f, 400);
    CHECK(walker.walk.gait() == WALK_GAIT::CRAWL);
    CHECK(walker.swaps == 3);
    CHECK(walker.swapsWhileMoving == 0);
}

// A stick pushed from the centre to the rim passes through the crawl's band on its way; the walk waits for it to
// settle, without moving, and starts in the trot.
static void autoStartsInTheGaitTheSettledStickAsks() {
    AutoWalker walker(WALK_GAIT::CRAWL);
    walker.walk.set_auto_gait(true);
    for (int tick = 1; tick <= 15; tick++) walker.hold(tick / 15.0f, 1);
    CHECK(walker.walk.steps().step_x == 0);
    CHECK(walker.walk.gait() == WALK_GAIT::CRAWL);
    walker.hold(1.0f, 100);
    CHECK(walker.walk.gait() == WALK_GAIT::TROT);
    CHECK(walker.walk.steps().step_x > 0);
    CHECK(walker.swaps == 1);
}

// A stick that wavers about the boundary while walking keeps the gait the walk is in, without a stop.
static void autoDoesNotChatterAtTheBoundary() {
    for (const float start : {0.1f, 0.6f}) {
        AutoWalker walker(WALK_GAIT::TROT);
        walker.walk.set_auto_gait(true);
        walker.hold(start, 300);
        const WALK_GAIT gait = walker.walk.gait();
        const int swaps = walker.swaps;
        float slowest = INFINITY;
        for (int pass = 0; pass < 20; pass++) {
            for (const float stick : {0.17f, 0.215f, 0.19f, 0.22f, 0.175f}) {
                walker.hold(stick, 10);
                if (pass > 0) slowest = std::min(slowest, std::fabs(walker.walk.steps().step_x));
            }
        }
        if (walker.swaps != swaps)
            std::printf("AUTO from stick %.1f swapped %d times on a wavering stick\n", start, walker.swaps - swaps);
        CHECK(walker.swaps == swaps);
        CHECK(walker.walk.gait() == gait);
        // No stop for a swap either: once the stroke had come up from the start, it stayed near the wavering stick's.
        const float stroke = gait == WALK_GAIT::CRAWL ? AUTO_CRAWL_STROKE : 1;
        CHECK(slowest > 0.15f * stroke * walker.config.max_step_length);
    }
}

// Swapping at a standstill moves no foot further in a tick than either gait does by itself over the same walk.
static void autoSwapsWithoutAJump() {
    const auto profile = [](AutoWalker &walker) {
        for (const float stick : {0.0f, 0.15f, 0.0f, 0.95f, 0.0f, 0.15f, 0.95f, 0.15f, 0.0f}) walker.hold(stick, 400);
    };
    AutoWalker autoWalker(WALK_GAIT::TROT), trot(WALK_GAIT::TROT), crawl(WALK_GAIT::CRAWL);
    autoWalker.walk.set_auto_gait(true);
    profile(autoWalker);
    profile(trot);
    profile(crawl);
    const float baseline = std::max(trot.worstFootStep, crawl.worstFootStep);
    if (autoWalker.worstFootStep > baseline + 1e-4f)
        std::printf("AUTO moved a foot %.2f mm in a tick, trot %.2f, crawl %.2f\n", autoWalker.worstFootStep * 1000,
                    trot.worstFootStep * 1000, crawl.worstFootStep * 1000);
    CHECK(autoWalker.swaps == 5);
    CHECK(autoWalker.swapsWhileMoving == 0);
    CHECK(autoWalker.worstFootStep <= baseline + 1e-4f);
}

// The crawl leaves the body over three feet when it stops; the trot's reach bound has no room for that, so AUTO's
// trot centres it before it walks.
static void autoCentresTheBodyTheCrawlShiftedBeforeItTrots() {
    AutoWalker walker(WALK_GAIT::CRAWL);
    walker.walk.set_auto_gait(true);
    walker.hold(0.05f, 300);
    walker.hold(0, 300);
    const float shifted = std::hypot(walker.body.xm, walker.body.zm);
    float shiftAtStart = -1;
    for (int tick = 0; tick < 200; tick++) {
        walker.hold(1.0f, 1);
        if (shiftAtStart < 0 && walker.walk.gait() == WALK_GAIT::TROT && walker.walk.steps().step_x > 0.001f)
            shiftAtStart = std::hypot(walker.body.xm, walker.body.zm);
    }
    CHECK(walker.walk.gait() == WALK_GAIT::TROT);
    if (!(shifted > 0.005f) || !(shiftAtStart >= 0 && shiftAtStart <= WalkState::TROT_CENTRED_M))
        std::printf("crawl shift %.2f mm, at the trot's start %.3f mm\n", shifted * 1000, shiftAtStart * 1000);
    CHECK(shifted > 0.005f);
    CHECK(shiftAtStart >= 0 && shiftAtStart <= WalkState::TROT_CENTRED_M);
    CHECK(std::hypot(walker.body.xm, walker.body.zm) <= 1e-4f);
}

// A step height changed mid-walk reaches a raised foot as eased as the walk's own swing moves it: taken in one tick,
// it moved the foot by the whole change at once.
static void aNewStepHeightEasesARaisedFootUp() {
    const KinConfig &config = *kinConfigFor(socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI);
    WalkState walk;
    walk.configure(config);
    MotionState &state = walk;
    body_state_t body;
    body.ym = config.default_body_height;
    body.updateFeet(config.default_feet_positions);
    state.begin();
    const CommandMsg low {0, 0.5f, 0, 0, 0.5f, 0.5f, 0.2f}, high {0, 0.5f, 0, 0, 0.5f, 0.5f, 1.0f};
    const auto worstLift = [&](int ticks) {
        float worst = 0;
        for (int t = 0; t < ticks; t++) {
            float before[4];
            for (int leg = 0; leg < 4; leg++) before[leg] = body.feet[leg][1];
            state.step(body, DT);
            for (int leg = 0; leg < 4; leg++) worst = std::max(worst, std::fabs(body.feet[leg][1] - before[leg]));
        }
        return worst;
    };
    state.handleCommand(high);
    worstLift(300);
    const float steadyHigh = worstLift(300);
    state.handleCommand(low);
    worstLift(300);

    // On, until a foot is halfway through its swing, at its highest.
    int raised = -1;
    for (int t = 0; t < 200 && raised < 0; t++) {
        state.step(body, DT);
        for (int leg = 0; leg < 4; leg++) {
            const float swing = (std::fmod(walk.phase_time + walk.phase_offset[leg], 1.0f) - walk.stand_offset) /
                                (1 - walk.stand_offset);
            if (swing > 0.45f && swing < 0.55f) raised = leg;
        }
    }
    CHECK(raised >= 0);
    if (raised < 0) return;
    const float before = body.feet[raised][1];
    state.handleCommand(high);
    state.step(body, DT);
    const float jump = std::fabs(body.feet[raised][1] - before);
    if (jump > steadyHigh)
        std::printf("a new step height moved a raised foot %.2f mm in a tick, the walk itself at most %.2f mm\n",
                    jump * 1000, steadyHigh * 1000);
    CHECK(jump <= steadyHigh);
    worstLift(100);
    CHECK(std::fabs(walk.steps().step_height - config.max_step_height) < 0.01f * config.max_step_height);
}

// AUTO asks the body for the stick's share of the trot's top speed, forward and turning, whichever gait walks it: at
// the same stroke the crawl's slower clock and longer stance walked the body about 4.5 times slower than the trot, so
// the speed jumped where AUTO swapped them.
static void autoCommandsOneSpeedPerStickInEitherGait() {
    WalkState trot;
    trot.set_mode_trot();
    const float trotRate = trot.stanceRate(0.5f); // stances per second; a full stick's stroke each is the top speed
    struct Case {
        float stick;
        WALK_GAIT start;
    };
    const Case cases[] = {{0.05f, WALK_GAIT::CRAWL},
                          {0.1f, WALK_GAIT::CRAWL},
                          {AUTO_CRAWL_MAX - 0.005f, WALK_GAIT::CRAWL},
                          {AUTO_CRAWL_MAX + 0.005f, WALK_GAIT::TROT},
                          {0.5f, WALK_GAIT::CRAWL},
                          {1.0f, WALK_GAIT::CRAWL}};
    for (const bool turning : {false, true}) {
        bool crawled = false, trotted = false;
        for (const Case &c : cases) {
            AutoWalker walker(c.start);
            walker.walk.set_auto_gait(true);
            walker.hold(turning ? 0 : c.stick, 300, turning ? c.stick : 0);
            const gait_state_t &steps = walker.walk.steps();
            const float stroke = turning ? steps.step_angle : steps.step_x / walker.config.max_step_length;
            const float share = stroke * walker.walk.stanceRate(0.5f) / trotRate;
            const bool crawling = walker.walk.gait() == WALK_GAIT::CRAWL;
            (crawling ? crawled : trotted) = true;
            if (std::fabs(share - c.stick) > 0.01f * c.stick)
                std::printf("AUTO %s at stick %.3f in the %s: %.3f of the trot's top speed\n",
                            turning ? "turning" : "forward", c.stick, crawling ? "crawl" : "trot", share);
            CHECK(std::fabs(share - c.stick) <= 0.01f * c.stick);
        }
        CHECK(crawled && trotted);
    }
}

// While every foot is down a new step height moves nothing, so a walk takes it at once: eased from the variant's
// default instead, a Yertle's first steps in the app's simulation lifted high enough to topple it.
static void aWalkLiftsItsFirstFootAtTheCommandedStepHeight() {
    const KinematicsVariant variants[] = {socket_message_KinematicsVariant_SPOTMICRO_ESP32,
                                          socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI,
                                          socket_message_KinematicsVariant_SPOTMICRO_YERTLE};
    for (KinematicsVariant variant : variants) {
        const KinConfig &config = *kinConfigFor(variant);
        WalkState walk;
        walk.configure(config);
        MotionState &state = walk;
        body_state_t body;
        body.ym = config.default_body_height;
        body.updateFeet(config.default_feet_positions);
        state.begin();
        state.handleCommand({0, 1, 0, 0, 0.5f, 0.5f, 0.5f});
        const float commanded = 0.5f * config.max_step_height;
        bool lifted = false;
        for (int t = 0; t < 100 && !lifted; t++) {
            state.step(body, DT);
            const auto down = walk.stance();
            lifted = !(down[0] && down[1] && down[2] && down[3]);
        }
        CHECK(lifted);
        if (std::fabs(walk.steps().step_height - commanded) > 1e-6f)
            std::printf("%s lifted its first foot at a step height of %.1f mm, commanded %.1f mm\n",
                        variantName(variant), walk.steps().step_height * 1000, commanded * 1000);
        CHECK(std::fabs(walk.steps().step_height - commanded) <= 1e-6f);
    }
}

// The stop a walk predicts is the one it takes: centring the sticks anywhere in the cycle, the odometry of the feet it
// walks until it stands still lands where stopStep said, walking, strafing and turning, slow and at a full stick.
// Within 3 mm and 1.5 degrees, measured up to 2.4 mm and 1.1 at a full stick: the odometry counts a foot from the
// second sample it is down at, and the prediction samples the stop every 20 ms where the walk ticks every 10 ms, slowed
// at a full stick by the joints' speed cap.
static void theStopAWalkPredictsIsTheOneItTakes() {
    const KinConfig &config = KIN_CONFIG_SPOTMICRO_ESP32_MINI;
    const float sticks[][3] = {{0, 1, 0}, {0, 0.3f, 0}, {-0.6f, 0, 0}, {0, 0, 1}, {0, 0, -0.3f}, {0.5f, 0.5f, 0.3f}};
    float worstPosition = 0, worstTurn = 0, longest = 0;
    for (const auto &stick : sticks) {
        for (int stopAt = 0; stopAt < 20; stopAt++) {
            WalkState walk;
            walk.configure(config);
            MotionState &state = walk;
            body_state_t body;
            body.ym = config.default_body_height;
            body.updateFeet(config.default_feet_positions);
            state.begin();
            state.handleCommand({stick[0], stick[1], stick[2], 0, 0.5f, 0.5f, 0.5f});
            for (int i = 0; i < 100 + 3 * stopAt; i++) state.step(body, DT);
            const BodyStep predicted = walk.stopStep(body);
            KinematicOdometry odometry;
            odometry.update(body, walk.stance());
            state.handleCommand({0, 0, 0, 0, 0.5f, 0.5f, 0.5f});
            int ticks = 0;
            for (; !walk.standsStill() && ticks < 300; ticks++) {
                state.step(body, DT);
                odometry.update(body, walk.stance());
            }
            CHECK(walk.standsStill());
            const OdometryEstimate &stopped = odometry.estimate();
            worstPosition =
                std::max(worstPosition, std::hypot(stopped.x - predicted.forward, stopped.y - predicted.left));
            worstTurn = std::max(worstTurn, std::fabs(stopped.yaw - predicted.turn));
            longest = std::max(longest, ticks * DT);
        }
    }
    std::printf("stop predictions: worst %.2f mm, %.3f degrees; the longest stop %.2f s\n", worstPosition * 1000,
                worstTurn * 180 / static_cast<float>(M_PI), longest);
    CHECK(worstPosition < 0.003f);
    CHECK(worstTurn < 1.5f * static_cast<float>(M_PI) / 180);
    CHECK(longest < WalkState::STOP_PREDICTION_DT_S * WalkState::STOP_PREDICTION_STEPS);
}

int main() {
    theStopAWalkPredictsIsTheOneItTakes();
    aStanceFootStaysDownAndLiftsOffLevelInEveryDirection();
    aWalkLiftsItsFirstFootAtTheCommandedStepHeight();
    autoCommandsOneSpeedPerStickInEitherGait();
    aNewStepHeightEasesARaisedFootUp();
    autoPicksTheGaitBySpeedWithHysteresis();
    autoSwapsTheGaitOnlyAtAStandstill();
    autoStartsInTheGaitTheSettledStickAsks();
    autoDoesNotChatterAtTheBoundary();
    autoSwapsWithoutAJump();
    autoCentresTheBodyTheCrawlShiftedBeforeItTrots();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
