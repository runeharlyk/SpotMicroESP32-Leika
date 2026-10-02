// Host test of telemetry/telemetry.h, built and run by test_host_programs.py.
#include <cstdio>
#include <telemetry/telemetry.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static socket_message_TickSample tick(uint32_t seq) {
    socket_message_TickSample t = socket_message_TickSample_init_zero;
    t.seq = seq;
    return t;
}

static void nothingIsRecordedWhileNobodyListens() {
    static Telemetry telemetry;
    for (uint32_t i = 0; i < 20; i++) telemetry.record(tick(i));
    static socket_message_TelemetryBatch batch;
    CHECK(!telemetry.takeBatch(batch));
    CHECK(telemetry.droppedTicks() == 0);
}

static void batchesAreWholeAndContiguous() {
    static Telemetry telemetry;
    telemetry.setRecording(true);
    for (uint32_t i = 0; i < 25; i++) telemetry.record(tick(i));
    static socket_message_TelemetryBatch batch;
    CHECK(telemetry.takeBatch(batch));
    CHECK(batch.batch_seq == 0 && batch.ticks_count == 10 && batch.ticks[0].seq == 0 && batch.ticks[9].seq == 9);
    CHECK(telemetry.takeBatch(batch));
    CHECK(batch.batch_seq == 1 && batch.ticks[0].seq == 10);
    CHECK(!telemetry.takeBatch(batch));  // five left: not a whole batch yet
    for (uint32_t i = 25; i < 30; i++) telemetry.record(tick(i));
    CHECK(telemetry.takeBatch(batch));
    CHECK(batch.batch_seq == 2 && batch.ticks[0].seq == 20 && batch.ticks[9].seq == 29);
}

static void aFullRingCountsWhatItDrops() {
    static Telemetry telemetry;
    telemetry.setRecording(true);
    for (uint32_t i = 0; i < Telemetry::RING_TICKS + 7; i++) telemetry.record(tick(i));
    CHECK(telemetry.droppedTicks() == 7);
    static socket_message_TelemetryBatch batch;
    CHECK(telemetry.takeBatch(batch));
    CHECK(batch.dropped_ticks == 7);
}

// A recorder that leaves mid-batch must not hand its leftovers to the next one, minutes later.
static void aNewRecordingStartsWithoutTheLastOnesLeftovers() {
    static Telemetry telemetry;
    telemetry.setRecording(true);
    for (uint32_t i = 0; i < 25; i++) telemetry.record(tick(i));
    static socket_message_TelemetryBatch batch;
    CHECK(telemetry.takeBatch(batch) && telemetry.takeBatch(batch));
    telemetry.setRecording(false);
    telemetry.discardUnsent();
    telemetry.setRecording(true);
    for (uint32_t i = 100; i < 110; i++) telemetry.record(tick(i));
    CHECK(telemetry.takeBatch(batch));
    CHECK(batch.ticks[0].seq == 100);
}

static void anImuSampleKeepsEveryField() {
    ImuSample sample;
    sample.t_us = 123456789012LL;
    sample.accel = {1, 2, 3};
    sample.gyro = {4, 5, 6};
    sample.mag = {7, 8, 9};
    sample.mag_t_us = 42;
    sample.gravity = {0, 0.5f, -0.866f};
    sample.quat = {0.9f, 0.1f, 0.2f, 0.3f};
    sample.rpy = {0.1f, 0.2f, 0.3f};
    sample.temperature = 25;
    sample.valid = ImuValid::ACCEL | ImuValid::MAG;
    socket_message_ImuSample proto = socket_message_ImuSample_init_zero;
    imuToProto(sample, proto);
    CHECK(proto.t_us == 123456789012ULL && proto.mag_t_us == 42);
    CHECK(proto.accel[2] == 3 && proto.gyro[0] == 4 && proto.mag[1] == 8 && proto.gravity[1] == 0.5f);
    CHECK(proto.quat[3] == 0.3f && proto.rpy[2] == 0.3f && proto.temperature == 25 && proto.valid == 5);
}

int main() {
    nothingIsRecordedWhileNobodyListens();
    batchesAreWholeAndContiguous();
    aFullRingCountsWhatItDrops();
    aNewRecordingStartsWithoutTheLastOnesLeftovers();
    anImuSampleKeepsEveryField();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
