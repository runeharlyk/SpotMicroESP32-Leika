// Host test of telemetry/spsc_ring.h, built and run by test_host_programs.py.
#include <atomic>
#include <cstdio>
#include <thread>
#include <telemetry/spsc_ring.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

struct Sample {
    uint32_t seq;
    uint32_t copies[63];  // every copy equals seq: a half-written slot shows as a mismatch
};

static Sample make(uint32_t seq) {
    Sample s {seq, {}};
    for (auto &copy : s.copies) copy = seq;
    return s;
}

static void itHoldsExactlyItsCapacity() {
    SpscRing<Sample, 64> ring;
    for (uint32_t i = 0; i < 64; i++) CHECK(ring.push(make(i)));
    CHECK(!ring.push(make(64)));
    CHECK(ring.size() == 64);
    Sample out;
    for (uint32_t i = 0; i < 64; i++) CHECK(ring.pop(out) && out.seq == i);
    CHECK(!ring.pop(out));
}

static void twoThreadsNeverSeeATornOrLostSample() {
    static SpscRing<Sample, 64> ring;
    constexpr uint32_t COUNT = 200000;
    std::thread producer([] {
        for (uint32_t i = 0; i < COUNT; i++)
            while (!ring.push(make(i))) std::this_thread::yield();
    });
    // The consumer drains to the end whatever it sees, so a broken ring fails the checks instead of hanging the
    // producer on a ring nobody empties.
    uint32_t received = 0;
    bool outOfOrder = false, torn = false;
    Sample out;
    while (received < COUNT) {
        if (!ring.pop(out)) continue;
        outOfOrder |= out.seq != received;
        for (uint32_t copy : out.copies) torn |= copy != out.seq;
        received++;
    }
    producer.join();
    CHECK(!outOfOrder);
    CHECK(!torn);
}

int main() {
    itHoldsExactlyItsCapacity();
    twoThreadsNeverSeeATornOrLostSample();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
