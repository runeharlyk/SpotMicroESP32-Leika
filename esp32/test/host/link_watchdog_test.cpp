// Host test of the WiFi link watchdog's policy, built and run by test_host_programs.py.
#include <cstdio>
#include <wifi/link_watchdog.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// Probes go out every period, one at a time: a probe still out holds back the next however late it is.
static void probesAreSpacedAndOneAtATime() {
    LinkWatchdog watchdog;
    watchdog.linkChanged(1000);
    CHECK(!watchdog.probeDue(5999));
    CHECK(watchdog.probeDue(6000));
    watchdog.probeStarted(6000);
    CHECK(!watchdog.probeDue(20000));
    CHECK(!watchdog.probeEnded(true));
    CHECK(!watchdog.probeDue(10999));
    CHECK(watchdog.probeDue(11000));
}

// Three unanswered probes in a row reconnect; an answer in between starts the count again.
static void onlyFailuresInARowReconnect() {
    LinkWatchdog watchdog;
    CHECK(!watchdog.probeEnded(false));
    CHECK(!watchdog.probeEnded(false));
    CHECK(!watchdog.probeEnded(true));
    CHECK(!watchdog.probeEnded(false));
    CHECK(!watchdog.probeEnded(false));
    CHECK(watchdog.probeEnded(false));
    // The reconnect starts afresh: it takes three more to ask again.
    CHECK(!watchdog.probeEnded(false));
    CHECK(!watchdog.probeEnded(false));
    CHECK(watchdog.probeEnded(false));
}

// A join or drop in the middle forgets earlier failures and a probe still out.
static void aLinkChangeStartsAfresh() {
    LinkWatchdog watchdog;
    watchdog.probeEnded(false);
    watchdog.probeEnded(false);
    watchdog.probeStarted(5000);
    watchdog.linkChanged(7000);
    CHECK(!watchdog.probeDue(11999));
    CHECK(watchdog.probeDue(12000));
    CHECK(!watchdog.probeEnded(false));
    CHECK(!watchdog.probeEnded(false));
    CHECK(watchdog.probeEnded(false));
}

int main() {
    probesAreSpacedAndOneAtATime();
    onlyFailuresInARowReconnect();
    aLinkChangeStartsAfresh();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
