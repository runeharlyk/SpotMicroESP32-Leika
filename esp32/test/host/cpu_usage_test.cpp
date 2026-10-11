// Host test of utils/cpu_usage.h against fake idle-task run times (stubs/freertos/task.h), built and run by
// test_host_programs.py.
#include <cstdio>
#include <utils/cpu_usage.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// The service loop samples every two seconds.
static void twoSeconds(uint32_t idle0Us, uint32_t idle1Us) {
    fake_timer::nowUs += 2000000;
    fake_rtos::idleRunTimeUs[0] += idle0Us;
    fake_rtos::idleRunTimeUs[1] += idle1Us;
}

// Before a second sample there is no window to measure: nothing is reported busy.
static void eachCoreIsBusyForWhatItsIdleTaskDidNotRun() {
    CpuUsage usage;
    fake_timer::nowUs = 5000000;
    usage.sample();
    CHECK(usage.core(0) == 0 && usage.core(1) == 0 && usage.total() == 0);
    // Core 0 runs the network, core 1 the control loop.
    twoSeconds(1500000, 500000);
    usage.sample();
    CHECK(usage.core(0) == 25);
    CHECK(usage.core(1) == 75);
    CHECK(usage.total() == 50);
    // Each window stands alone.
    twoSeconds(2000000, 1000000);
    usage.sample();
    CHECK(usage.core(0) == 0);
    CHECK(usage.core(1) == 50);
    CHECK(usage.total() == 25);
}

// The run-time counters are 32 bits of microseconds and wrap after 71 minutes; a window across the wrap is exact.
static void aWindowAcrossTheCounterWrapIsExact() {
    CpuUsage usage;
    fake_rtos::idleRunTimeUs[0] = 0xFFFFFFFFu - 400000;
    fake_rtos::idleRunTimeUs[1] = 0xFFFFFFFFu - 100000;
    usage.sample();
    twoSeconds(1000000, 1800000);
    usage.sample();
    CHECK(usage.core(0) == 50);
    CHECK(usage.core(1) == 10);
}

// An idle task counted beyond the window, as a context switch just after the sample makes it, is no core below zero.
static void idleTimeBeyondTheWindowIsAnIdleCore() {
    CpuUsage usage;
    usage.sample();
    twoSeconds(2100000, 2000000);
    usage.sample();
    CHECK(usage.core(0) == 0);
    CHECK(usage.total() == 0);
}

int main() {
    eachCoreIsBusyForWhatItsIdleTaskDidNotRun();
    aWindowAcrossTheCounterWrapIsExact();
    idleTimeBeyondTheWindowIsAnIdleCore();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
