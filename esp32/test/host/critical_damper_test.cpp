// Host test of utils/critical_damper.h, built and run by test_host_programs.py.
#include <cmath>
#include <cstdio>
#include <vector>
#include <utils/critical_damper.h>

static int failures = 0;

#define CHECK(condition)                                                         do {                                                                             if (!(condition)) {                                                              std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);             failures++;                                                              }                                                                        } while (0)

constexpr float SETTLE_S = 0.333f;
constexpr float OMEGA = CriticalDamper::omegaFor(SETTLE_S);

// A step from 0 to 1, sampled every dt for the given time.
static std::vector<float> stepResponse(float dt, float seconds) {
    CriticalDamper damper;
    float value = 0;
    std::vector<float> path;
    for (int tick = 0; tick < static_cast<int>(std::lround(seconds / dt)); tick++) {
        value = damper.step(value, 1, dt, OMEGA);
        path.push_back(value);
    }
    return path;
}

static void aStepSettlesInItsTime() {
    const std::vector<float> path = stepResponse(0.01f, 1);
    size_t settled = 0;
    while (settled < path.size() && path[settled] < 0.95f) settled++;
    const float seconds = 0.01f * (settled + 1);
    CHECK(seconds >= SETTLE_S - 0.011f && seconds <= SETTLE_S + 0.011f);
}

static void itNeverOvershoots() {
    const std::vector<float> path = stepResponse(0.01f, 2);
    for (size_t i = 0; i < path.size(); i++) {
        CHECK(path[i] <= 1);
        if (i > 0) CHECK(path[i] >= path[i - 1]);
    }
}

// The velocity ramps up instead of jumping with the target: after the first 10 ms the value has barely moved,
// where a first-order filter settling as fast would already have covered 9% of the step.
static void itStartsGently() { CHECK(stepResponse(0.01f, 0.01f)[0] < 0.02f); }

// The same path at 50 Hz as at 100 Hz, so the smoothing does not depend on the loop rate.
static void theLoopRateDoesNotChangeThePath() {
    const std::vector<float> fast = stepResponse(0.01f, 0.6f);
    const std::vector<float> slow = stepResponse(0.02f, 0.6f);
    for (size_t i = 0; i < slow.size(); i++) CHECK(std::fabs(slow[i] - fast[2 * i + 1]) < 1e-5f);
}

int main() {
    aStepSettlesInItsTime();
    itNeverOvershoots();
    itStartsGently();
    theLoopRateDoesNotChangeThePath();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
