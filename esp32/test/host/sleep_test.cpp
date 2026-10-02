// Host test of utils/sleep.h against a fake tick (stubs/freertos/task.h) and busy-wait (stubs/esp_rom_sys.h),
// built and run by test_host_programs.py.
#include <cstdio>
#include <initializer_list>
#include <utils/sleep.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// vTaskDelay(n) wakes on the n-th tick interrupt, so from a call just before a tick it sleeps only n - 1
// whole ticks, and from a call just after one nearly n.
static uint64_t shortestWaitUs(uint32_t ticks, uint32_t tickUs) { return ticks ? (ticks - 1) * uint64_t(tickUs) : 0; }
static uint64_t longestWaitUs(uint32_t ticks, uint32_t tickUs) { return ticks * uint64_t(tickUs); }

static void ticksCoverTheRequestedTimeWhereverInATickTheCallFalls() {
    for (uint32_t hz : {100u, 1000u}) {
        const uint32_t tickUs = 1000000 / hz;
        for (uint32_t ms = 0; ms <= 1000; ms++) {
            const TickType_t ticks = ticksCovering(ms, hz);
            CHECK(shortestWaitUs(ticks, tickUs) >= ms * 1000ull);
            CHECK(longestWaitUs(ticks, tickUs) < ms * 1000ull + 2 * tickUs);
        }
    }
}

// The drivers' waits: BMP180 conversions (5 and 26 ms), PCA9685 oscillator start (5 ms), the MPU6050
// calibration's 1 ms between samples, and the BNO055's 650 ms reset.
static void sleepsWaitAtLeastTheRequestedTimeAtTheFirmwaresTickRate() {
    const uint32_t tickUs = 1000000 / configTICK_RATE_HZ;
    for (uint32_t ms = 0; ms <= 1000; ms++) {
        fake_rtos::delayedTicks = 0;
        fake_rom::spunUs = 0;
        sleepAtLeastMs(ms);
        const uint64_t shortest = fake_rom::spunUs + shortestWaitUs(fake_rtos::delayedTicks, tickUs);
        const uint64_t longest = fake_rom::spunUs + longestWaitUs(fake_rtos::delayedTicks, tickUs);
        CHECK(shortest >= ms * 1000ull);
        CHECK(longest < ms * 1000ull + 2 * tickUs);
    }
}

// A millisecond between calibration samples must not become a whole tick: 1200 samples would take 12 s.
static void aWaitShorterThanATickDoesNotSleepAWholeTick() {
    fake_rtos::delayedTicks = 0;
    fake_rom::spunUs = 0;
    sleepAtLeastMs(1);
    CHECK(fake_rtos::delayedTicks == 0);
    CHECK(fake_rom::spunUs == 1000);
}

int main() {
    ticksCoverTheRequestedTimeWhereverInATickTheCallFalls();
    sleepsWaitAtLeastTheRequestedTimeAtTheFirmwaresTickRate();
    aWaitShorterThanATickDoesNotSleepAWholeTick();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
