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

int main() {
    aStanceFootStaysDownAndLiftsOffLevelInEveryDirection();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
