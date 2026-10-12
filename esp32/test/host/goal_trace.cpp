// Drives the firmware's goal controller through a few goals on a lagging, short-stepping body and prints every tick
// as JSON, so the simulation's port (simulation/src/robot/firmware_goal.py) can be pinned to it. Written to
// simulation/tests/fixtures/firmware-goal-trace.json by export_goal_trace.py; test_host_programs.py checks it is current.
#include <cstdio>
#include <goal_controller.h>

namespace {

constexpr float DT = 0.01f;
constexpr float DEG = static_cast<float>(M_PI) / 180.0f;

struct Scenario {
    Goal goal;
    float topSpeed, topTurn;
    float scale; // of what the sticks ask that the body does
    float drift; // rad/s of heading the body picks up on its own
};

const Scenario SCENARIOS[] = {
    {{0.2f, 0, 0, 0.5f}, 0.128f, 1.33f, 0.9f, 0.05f},
    {{0, -0.1f, 0, 0.8f}, 0.128f, 1.33f, 1.1f, 0},
    {{0, 0, 90 * DEG, 0.5f}, 0.128f, 1.33f, 0.85f, 0},
    {{0, 0, -200 * DEG, 1}, 0.15f, 1.0f, 1, 0},
    {{0.1f, 0, 0, 0.5f}, 0.128f, 1.33f, 0, 0}, // a body that cannot move: it times out
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
        std::printf("\"top_speed\":%.9g,\"top_turn\":%.9g,\"timeout\":%.9g,\"ticks\":[", sc.topSpeed, sc.topTurn,
                    goal.timeout());
        GoalSticks moving, sticks;
        BodyStep step;
        float dt = 0;
        for (int tick = 0; tick == 0 || goal.running(); tick++) {
            sticks = goal.update(step, dt);
            std::printf("%s{\"step\":[%.9g,%.9g,%.9g],\"dt\":%.9g,\"sticks\":[%.9g,%.9g,%.9g],\"state\":%d,"
                        "\"progress\":%.9g}",
                        tick ? "," : "", step.forward, step.left, step.turn, dt, sticks.forward, sticks.left,
                        sticks.turn, static_cast<int>(goal.state()), goal.progress());
            // The gait's command filter, as a first-order lag of 100 ms.
            const float k = DT / (0.1f + DT);
            moving.forward += k * (sticks.forward - moving.forward);
            moving.left += k * (sticks.left - moving.left);
            moving.turn += k * (sticks.turn - moving.turn);
            step = {moving.forward * sc.topSpeed * sc.scale * DT, moving.left * sc.topSpeed * sc.scale * DT,
                    (moving.turn * sc.topTurn * sc.scale + sc.drift) * DT};
            dt = DT;
        }
        std::printf("]}");
    }
    std::printf("]}\n");
    return 0;
}
