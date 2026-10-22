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

// The bands: the crawl below 0.30 of a full stick, the trot from there up, and each gait holds its band 0.06 further.
static void autoPicksTheGaitBySpeedWithHysteresis() {
    CHECK(selectAutoGait(0.0f, WALK_GAIT::TROT) == WALK_GAIT::CRAWL);
    CHECK(selectAutoGait(0.29f, WALK_GAIT::TROT) == WALK_GAIT::CRAWL);
    CHECK(selectAutoGait(0.30f, WALK_GAIT::TROT) == WALK_GAIT::TROT);
    CHECK(selectAutoGait(1.0f, WALK_GAIT::CRAWL) == WALK_GAIT::TROT);
    // Between the bands' edges the gait in force stays.
    for (const float demand : {0.30f, 0.33f, 0.35f}) {
        CHECK(selectAutoGait(demand, WALK_GAIT::CRAWL) == WALK_GAIT::CRAWL);
        CHECK(selectAutoGait(demand, WALK_GAIT::TROT) == WALK_GAIT::TROT);
    }
    CHECK(selectAutoGait(0.36f, WALK_GAIT::CRAWL) == WALK_GAIT::TROT);
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

    // Holds the forward stick `ly` for `ticks` ticks.
    void hold(float ly, int ticks) {
        static_cast<MotionState &>(walk).handleCommand({0, ly, 0, 0, 0.5f, 0.5f, 0.5f});
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
    walker.hold(0.2f, 300);
    CHECK(walker.walk.gait() == WALK_GAIT::CRAWL);
    CHECK(walker.walk.steps().step_x > 0);
    walker.hold(1.0f, 400);
    CHECK(walker.walk.gait() == WALK_GAIT::TROT);
    CHECK(walker.walk.steps().step_x > 0.9f * walker.config.max_step_length);
    walker.hold(0.2f, 400);
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
    for (const float start : {0.2f, 0.6f}) {
        AutoWalker walker(WALK_GAIT::TROT);
        walker.walk.set_auto_gait(true);
        walker.hold(start, 300);
        const WALK_GAIT gait = walker.walk.gait();
        const int swaps = walker.swaps;
        float slowest = INFINITY;
        for (int pass = 0; pass < 20; pass++) {
            for (const float stick : {0.30f, 0.35f, 0.32f, 0.355f, 0.31f}) {
                walker.hold(stick, 10);
                if (pass > 0) slowest = std::min(slowest, std::fabs(walker.walk.steps().step_x));
            }
        }
        if (walker.swaps != swaps)
            std::printf("AUTO from stick %.1f swapped %d times on a wavering stick\n", start, walker.swaps - swaps);
        CHECK(walker.swaps == swaps);
        CHECK(walker.walk.gait() == gait);
        // No stop for a swap either: once the stroke had come up from the start, it stayed near the wavering stick's.
        CHECK(slowest > 0.25f * walker.config.max_step_length);
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
    walker.hold(0.2f, 300);
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

int main() {
    aStanceFootStaysDownAndLiftsOffLevelInEveryDirection();
    autoPicksTheGaitBySpeedWithHysteresis();
    autoSwapsTheGaitOnlyAtAStandstill();
    autoStartsInTheGaitTheSettledStickAsks();
    autoDoesNotChatterAtTheBoundary();
    autoSwapsWithoutAJump();
    autoCentresTheBodyTheCrawlShiftedBeforeItTrots();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
