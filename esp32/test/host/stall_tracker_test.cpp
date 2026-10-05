// Host test of stall_tracker.h, built and run by test_host_programs.py.
#include <cstdio>
#include <communication/stall_tracker.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void aSocketFullForLongerThanTheLimitHasGone() {
    StallTracker tracker(10000);
    CHECK(!tracker.gone(5, false, 1000));
    CHECK(!tracker.gone(5, false, 11000));
    CHECK(tracker.gone(5, false, 11001));
}

static void aSocketThatDrainsStartsOver() {
    StallTracker tracker(10000);
    tracker.gone(5, false, 1000);
    CHECK(!tracker.gone(5, true, 9000));
    CHECK(!tracker.gone(5, false, 15000));
    CHECK(!tracker.gone(5, false, 25000));
    CHECK(tracker.gone(5, false, 25001));
}

static void clientsAreTrackedApart() {
    StallTracker tracker(10000);
    tracker.gone(5, false, 0);
    CHECK(!tracker.gone(6, false, 20000));
    CHECK(tracker.gone(5, false, 20000));
}

// A descriptor is reused for the next connection, which must not inherit the last one's stall.
static void aForgottenClientStartsOver() {
    StallTracker tracker(10000);
    tracker.gone(5, false, 0);
    tracker.forget(5);
    CHECK(!tracker.gone(5, false, 20000));
}

int main() {
    aSocketFullForLongerThanTheLimitHasGone();
    aSocketThatDrainsStartsOver();
    clientsAreTrackedApart();
    aForgottenClientStartsOver();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
