// Drives the firmware's goal controller through a few goals on a lagging, short-stepping, drifting body and prints
// every tick as JSON, so the simulation's port (simulation/src/robot/firmware_goal.py) can be pinned to it. Written to
// simulation/tests/fixtures/firmware-goal-trace.json by export_goal_trace.py; test_host_programs.py checks it is
// current.
#include <cmath>
#include <cstdio>
#include <goal_controller.h>

namespace {

constexpr float DT = 0.01f;
constexpr float DEG = static_cast<float>(M_PI) / 180.0f;

struct Scenario {
    Goal goal;
    float topSpeed, topTurn;
    float scale;    // of what the sticks ask that the body does
    float drift;    // rad/s of heading the body picks up per full stick it walks
    float predicts; // the share of its coast the body says it has left
    float lag;      // s: the gait's command filter, as a first-order lag
};

const Scenario SCENARIOS[] = {
    {{0.2f, 0, 0, 0.5f}, 0.128f, 1.33f, 0.9f, 0.1f, 1, 0.1f},
    {{0, -0.1f, 0, 0.8f}, 0.128f, 1.33f, 1.1f, -0.05f, 1, 0.1f},
    {{0, 0, 90 * DEG, 0.5f}, 0.128f, 1.33f, 0.85f, 0, 1, 0.1f},
    {{0, 0, -200 * DEG, 1}, 0.15f, 1.0f, 1, 0, 1, 0.1f},
    {{0.3f, 0, 0, 1}, 0.4f, 1.33f, 1, 0, 1, 0.1f},           // fast enough that its stop sets the slowdown zone
    {{0, 0, 20 * DEG, 1}, 0.128f, 1.33f, 1, 0, 0.2f, 0.25f}, // it undercounts its coast: a corrective pass
    {{0, 0, 20 * DEG, 1}, 0.128f, 1.33f, 1, 0, 0, 0.25f},    // it counts no coast: it overshoots every pass
    {{0.1f, 0, 0, 0.5f}, 0.128f, 1.33f, 0, 0, 1, 0.1f},      // a body that cannot move: it times out
};

} // namespace

int main() {
    std::printf("{\"dt\":%.9g,\"scenarios\":[", DT);
    for (size_t n = 0; n < sizeof(SCENARIOS) / sizeof(SCENARIOS[0]); n++) {
        const Scenario &sc = SCENARIOS[n];
        GoalController goal;
        goal.start(sc.goal, sc.topSpeed, sc.topTurn);
        std::printf("%s{\"goal\":{\"forward\":%.9g,\"sideways\":%.9g,\"turn\":%.9g,\"speed\":%.9g},", n ? "," : "",
                    sc.goal.forward, sc.goal.sideways, sc.goal.turn, sc.goal.speed);
        std::printf("\"top_speed\":%.9g,\"top_turn\":%.9g,\"ticks\":[", sc.topSpeed, sc.topTurn);
        GoalSticks moving, sticks;
        BodyStep step;
        float dt = 0;
        for (int tick = 0; tick == 0 || goal.running(); tick++) {
            const float coast = sc.predicts * sc.lag * sc.scale;
            const BodyStep toRest {moving.forward * sc.topSpeed * coast, moving.left * sc.topSpeed * coast,
                                   moving.turn * sc.topTurn * coast};
            const bool atRest =
                std::fabs(moving.forward) < 1e-3f && std::fabs(moving.left) < 1e-3f && std::fabs(moving.turn) < 1e-3f;
            sticks = goal.update(step, toRest, atRest, dt);
            std::printf(
                "%s{\"step\":[%.9g,%.9g,%.9g],\"to_rest\":[%.9g,%.9g,%.9g],\"at_rest\":%s,\"dt\":%.9g,"
                "\"sticks\":[%.9g,%.9g,%.9g],\"state\":%d,\"progress\":%.9g,\"passes\":%d,\"timeout\":%.9g}",
                tick ? "," : "", step.forward, step.left, step.turn, toRest.forward, toRest.left, toRest.turn,
                atRest ? "true" : "false", dt, sticks.forward, sticks.left, sticks.turn, static_cast<int>(goal.state()),
                goal.progress(), goal.passes(), goal.timeout());
            const float k = DT / (sc.lag + DT);
            moving.forward += k * (sticks.forward - moving.forward);
            moving.left += k * (sticks.left - moving.left);
            moving.turn += k * (sticks.turn - moving.turn);
            const float walked = std::fabs(moving.forward) + std::fabs(moving.left);
            step = {moving.forward * sc.topSpeed * sc.scale * DT, moving.left * sc.topSpeed * sc.scale * DT,
                    (moving.turn * sc.topTurn * sc.scale + sc.drift * walked) * DT};
            dt = DT;
        }
        std::printf("]}");
    }
    std::printf("]}\n");
    return 0;
}
