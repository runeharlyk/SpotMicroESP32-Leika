// Host test of a subscription on CommAdapterBase (communication/comm_base.hpp), on a fake link that decodes what it
// sends (fake_link.h), built and run by test_host_programs.py with platform_shared/topics.json as its argument. A
// client subscribes to a tag with sub_notif. A state topic answers with its owner's current value (the snapshot), then
// with the sub_notif echoed as the ack, and pushes every change after; a stream answers with the ack alone.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "fake_link.h"
#include <communication/topics.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_FRAMES(frames, ...) checkFrames(__VA_ARGS__, frames, __LINE__)

namespace {

constexpr int32_t MODE_TAG = socket_message_Message_mode_tag;
constexpr int32_t GAIT_TAG = socket_message_Message_walk_gait_tag;
constexpr int32_t IMU_TAG = socket_message_Message_imu_tag;
constexpr int32_t BATCH_TAG = socket_message_Message_telemetry_batch_tag;

// The mode's owner, as MotionService::mode is: read by the snapshot, changed by another task.
std::atomic<socket_message_ModesEnum> ownerMode {socket_message_ModesEnum_DEACTIVATED};
// Runs between the snapshot's read of the owner and its put, where another task may change the owner.
std::function<void()> afterModeRead;

void modeSnapshot(SnapshotSink& sink) {
    socket_message_ModeData current = socket_message_ModeData_init_zero;
    current.mode = ownerMode.load();
    if (afterModeRead) {
        auto run = std::move(afterModeRead);
        afterModeRead = nullptr;
        run();
    }
    sink.put(current);
}

void gaitSnapshot(SnapshotSink& sink) {
    socket_message_WalkGaitData current = socket_message_WalkGaitData_init_zero;
    current.gait = socket_message_WalkGaits_CRAWL;
    sink.put(current);
}

constexpr Topic TOPICS[] = {
    {IMU_TAG, TopicKind::STREAM, nullptr},
    {MODE_TAG, TopicKind::STATE, modeSnapshot},
    {GAIT_TAG, TopicKind::STATE, gaitSnapshot},
};

// A frame as a line: "mode 2", "ack 130".
std::string describe(const Frame& frame) {
    const socket_message_Message& m = frame.message;
    switch (m.which_message) {
        case socket_message_Message_sub_notif_tag: return "ack " + std::to_string(m.message.sub_notif.tag);
        case socket_message_Message_mode_tag: return "mode " + std::to_string((int)m.message.mode.mode);
        case socket_message_Message_walk_gait_tag: return "gait " + std::to_string((int)m.message.walk_gait.gait);
        case socket_message_Message_telemetry_header_tag: return "header";
        default: return "tag " + std::to_string((int)m.which_message);
    }
}

void checkFrames(const std::vector<std::string>& expected, const std::vector<Frame>& frames, int line) {
    std::string actual;
    for (const Frame& frame : frames) actual += describe(frame) + "; ";
    std::string wanted;
    for (const std::string& entry : expected) wanted += entry + "; ";
    if (actual == wanted) return;
    std::printf("FAIL %s:%d: sent [%s], expected [%s]\n", __FILE__, line, actual.c_str(), wanted.c_str());
    failures++;
}

socket_message_ModeData modeData(socket_message_ModesEnum mode) {
    socket_message_ModeData data = socket_message_ModeData_init_zero;
    data.mode = mode;
    return data;
}

const std::string STAND = "mode " + std::to_string((int)socket_message_ModesEnum_STAND);
const std::string WALK = "mode " + std::to_string((int)socket_message_ModesEnum_WALK);
const std::string DEACTIVATED = "mode " + std::to_string((int)socket_message_ModesEnum_DEACTIVATED);
const std::string MODE_ACK = "ack " + std::to_string(MODE_TAG);

void reset() {
    ownerMode = socket_message_ModesEnum_DEACTIVATED;
    afterModeRead = nullptr;
}

void nothing(SnapshotSink&) {}

} // namespace

static void aSubscribeToAStateTopicIsAnsweredWithTheCurrentValueThenTheAck() {
    reset();
    FakeLink link(TOPICS);
    ownerMode = socket_message_ModesEnum_WALK;

    link.subscribeTo(MODE_TAG, 3);

    CHECK_FRAMES(link.to(3), {WALK, MODE_ACK});
}

static void aSecondClientGetsItsOwnSnapshotAndTheFirstNothing() {
    reset();
    FakeLink link(TOPICS);
    link.subscribeTo(MODE_TAG, 3);
    link.forget();

    link.subscribeTo(MODE_TAG, 4);

    CHECK_FRAMES(link.to(4), {DEACTIVATED, MODE_ACK});
    CHECK(link.to(3).empty());
}

// The app resubscribes every tag on a reconnect, so the robot hears a subscribe from a client that already has it: it
// answers again, but a second entry on the list would send every push twice.
static void aRepeatedSubscribeIsAnsweredAgainAndGetsEachPushOnce() {
    reset();
    FakeLink link(TOPICS);
    link.subscribeTo(MODE_TAG, 3);
    link.subscribeTo(MODE_TAG, 3);
    CHECK_FRAMES(link.to(3), {DEACTIVATED, MODE_ACK, DEACTIVATED, MODE_ACK});
    link.forget();

    ownerMode = socket_message_ModesEnum_STAND;
    link.push(MODE_TAG);

    CHECK_FRAMES(link.to(3), {STAND});
}

static void oneUnsubscribeEndsARepeatedSubscription() {
    reset();
    FakeLink link(TOPICS);
    link.subscribeTo(MODE_TAG, 3);
    link.subscribeTo(MODE_TAG, 3);
    link.unsubscribeFrom(MODE_TAG, 3);
    link.forget();

    link.push(MODE_TAG);
    link.emit(modeData(socket_message_ModesEnum_STAND));

    CHECK(!link.hasSubscribers(MODE_TAG));
    CHECK(link.sentCount() == 0);
}

static void aStreamAndAnUnknownTagGetTheAckAlone() {
    reset();
    FakeLink link(TOPICS);

    link.subscribeTo(IMU_TAG, 3);
    link.subscribeTo(999, 3);

    CHECK_FRAMES(link.to(3), {"ack " + std::to_string(IMU_TAG), "ack 999"});
}

// The snapshot is the owner's value at the subscribe, and a change after it is pushed after the ack.
static void theSnapshotIsTheValueAtTheSubscribeAndALaterPushFollowsTheAck() {
    reset();
    FakeLink link(TOPICS);
    ownerMode = socket_message_ModesEnum_STAND;

    link.subscribeTo(MODE_TAG, 3);
    ownerMode = socket_message_ModesEnum_WALK;
    link.push(MODE_TAG);

    CHECK_FRAMES(link.to(3), {STAND, MODE_ACK, WALK});
}

// The ordering the design names as its top risk: the owner changes after the snapshot read it and before the snapshot
// went out, and the service loop pushes the change from its own task. Sent first, that push would be overtaken by the
// older snapshot, and the client would show a mode the robot has left, as live. The push waits for the snapshot, and
// reads the owner itself once it may send.
static void aChangeMadeWhileTheSnapshotIsSentIsPushedAfterIt() {
    reset();
    FakeLink link(TOPICS);
    std::thread pusher;
    std::promise<void> pushed;
    std::future<void> pushedNow = pushed.get_future();
    afterModeRead = [&] {
        pusher = std::thread([&] {
            ownerMode = socket_message_ModesEnum_STAND;
            link.push(MODE_TAG);
            pushed.set_value();
        });
        // A push that nothing holds back lands within this wait.
        pushedNow.wait_for(std::chrono::milliseconds(200));
    };

    link.subscribeTo(MODE_TAG, 3);
    if (pusher.joinable()) pusher.join();

    CHECK_FRAMES(link.to(3), {DEACTIVATED, MODE_ACK, STAND});
}

// A state push the client cannot take now is dropped, as every frame is; left at that, the client would show the value
// before it until the next change. The service loop's resend sends the current value once the client can take it.
static void aDroppedStatePushIsResentOnceAsTheCurrentValue() {
    reset();
    FakeLink link(TOPICS);
    link.subscribeTo(MODE_TAG, 3);
    link.forget();
    link.full.insert(3);
    ownerMode = socket_message_ModesEnum_STAND;
    CHECK(!link.push(MODE_TAG));
    ownerMode = socket_message_ModesEnum_WALK;
    link.push(MODE_TAG);
    link.resendDirty();
    CHECK(link.sentCount() == 0);

    link.full.clear();
    link.resendDirty();
    link.resendDirty();

    CHECK_FRAMES(link.to(3), {WALK});
}

// A state value emitted rather than pushed is marked the same way when the client misses it.
static void aDroppedEmitOfAStateTopicIsResentToo() {
    reset();
    FakeLink link(TOPICS);
    link.subscribeTo(MODE_TAG, 3);
    link.forget();
    link.full.insert(3);
    ownerMode = socket_message_ModesEnum_WALK;
    link.emit(modeData(socket_message_ModesEnum_WALK));

    link.full.clear();
    link.resendDirty();

    CHECK_FRAMES(link.to(3), {WALK});
}

// A send can fail on a client that looked ready: a BLE notify without an mbuf, a serial write cut short. The push is
// then lost as a dropped one is, and resent the same way; a resend that fails too keeps the mark for the next pass.
static void aStatePushWhoseSendFailsIsResentOnceItGoesOut() {
    reset();
    FakeLink link(TOPICS);
    link.subscribeTo(MODE_TAG, 3);
    link.forget();
    link.sendsFail = true;
    ownerMode = socket_message_ModesEnum_STAND;
    link.push(MODE_TAG);
    link.resendDirty();
    CHECK(link.sentCount() == 0);

    link.sendsFail = false;
    link.resendDirty();
    link.resendDirty();

    CHECK_FRAMES(link.to(3), {STAND});
}

// A subscribe's snapshot that does not go out is followed by no ack: the app, without it, subscribes again, and the
// subscription is marked, so the current value is resent once a send goes out, even if the app never asks again.
static void aSubscribeWhoseSnapshotFailsIsLeftUnackedAndResent() {
    reset();
    FakeLink link(TOPICS);
    ownerMode = socket_message_ModesEnum_WALK;
    link.failNext = 1;

    link.subscribeTo(MODE_TAG, 3);
    CHECK(link.sentCount() == 0);
    link.resendDirty();
    link.resendDirty();

    CHECK_FRAMES(link.to(3), {WALK});
}

// The answer goes out on the link's task under its encode lock, so one that waited on a client that cannot take a
// frame would hold every push to the link's other clients. That client gets neither snapshot nor ack, and its
// subscription is marked for resendDirty.
static void aSubscribeFromAClientThatCannotTakeAFrameIsAnsweredLater() {
    reset();
    FakeLink link(TOPICS);
    link.full.insert(3);

    link.subscribeTo(MODE_TAG, 3);
    link.subscribeTo(IMU_TAG, 3);
    CHECK(link.sentCount() == 0);
    CHECK(link.hasSubscribers(MODE_TAG));

    link.full.clear();
    ownerMode = socket_message_ModesEnum_STAND;
    link.resendDirty();
    CHECK_FRAMES(link.to(3), {STAND});
}

// A stream's next frame supersedes the one dropped, and a client that took every push has nothing to catch up on.
static void aDroppedStreamFrameAndADeliveredPushAreNotResent() {
    reset();
    FakeLink link(TOPICS);
    link.subscribeTo(IMU_TAG, 3);
    link.subscribeTo(MODE_TAG, 4);
    link.forget();
    link.full.insert(3);
    link.emit(socket_message_IMUData(socket_message_IMUData_init_zero));
    link.full.clear();
    ownerMode = socket_message_ModesEnum_STAND;
    link.push(MODE_TAG);

    link.resendDirty();

    CHECK(link.to(3).empty());
    CHECK_FRAMES(link.to(4), {STAND});
}

// A client that is gone takes its marks with it: nothing is resent to whoever gets its id next.
static void aClientThatLeavesIsResentNothing() {
    reset();
    FakeLink link(TOPICS);
    link.subscribeTo(MODE_TAG, 3);
    link.forget();
    link.full.insert(3);
    link.push(MODE_TAG);
    link.full.clear();
    link.unsubscribeFrom(MODE_TAG, 3);

    link.resendDirty();

    CHECK(link.sentCount() == 0);
}

// A push to a topic no one follows, or to a stream, sends nothing: a stream has no current value to read.
static void aPushGoesOnlyToAStateTopicsSubscribers() {
    reset();
    FakeLink link(TOPICS);
    link.subscribeTo(IMU_TAG, 3);
    link.forget();

    CHECK(link.push(MODE_TAG));
    CHECK(link.push(IMU_TAG));

    CHECK(link.sentCount() == 0);
}

// What a subscribe's listener sends, such as the telemetry header, comes before the ack, so the ack confirms it too.
static void aSubscribesListenerSendsBeforeTheAck() {
    reset();
    FakeLink link(TOPICS);
    link.onSubscribed([&link](int32_t tag, int cid) {
        if (tag == BATCH_TAG) link.emit(socket_message_TelemetryHeader(socket_message_TelemetryHeader_init_zero), cid);
    });

    link.subscribeTo(BATCH_TAG, 3);

    CHECK_FRAMES(link.to(3), {"header", "ack " + std::to_string(BATCH_TAG)});
}

// platform_shared/topics.json, the table the app's is checked against too: each entry's tag and kind, in file order.
static std::vector<TopicKindEntry> sharedTopics(const char* path) {
    std::ifstream file(path);
    std::vector<TopicKindEntry> topics;
    if (!file.good()) {
        std::printf("FAIL %s not found\n", path);
        failures++;
        return topics;
    }
    std::stringstream text;
    text << file.rdbuf();
    const std::string json = text.str();
    for (size_t at = json.find("\"tag\":"); at != std::string::npos; at = json.find("\"tag\":", at + 1)) {
        const int32_t tag = std::stoi(json.substr(at + 6));
        const size_t kindAt = json.find("\"kind\":", at);
        const size_t valueAt = json.find('"', kindAt + 7) + 1;
        const std::string kind = json.substr(valueAt, json.find('"', valueAt) - valueAt);
        CHECK(kind == "state" || kind == "stream");
        topics.push_back({tag, kind == "state" ? TopicKind::STATE : TopicKind::STREAM});
    }
    return topics;
}

static void theTopicTableMatchesTheSharedOne(const char* path) {
    const std::vector<TopicKindEntry> shared = sharedTopics(path);

    CHECK(std::size(TOPIC_KINDS) == shared.size());
    for (size_t i = 0; i < shared.size() && i < std::size(TOPIC_KINDS); i++) {
        CHECK(shared[i].tag == TOPIC_KINDS[i].tag);
        CHECK(shared[i].kind == TOPIC_KINDS[i].kind);
        CHECK(isStateTopic(shared[i].tag) == (shared[i].kind == TopicKind::STATE));
    }
    CHECK(!isStateTopic(999));
}

// main.cpp asserts its table with servesEveryTopic at compile time; here the check itself is checked.
static void aTableServesEveryTopicOnlyWithEachTopicAndASnapshotForEachState() {
    Topic table[std::size(TOPIC_KINDS)];
    for (size_t i = 0; i < std::size(TOPIC_KINDS); i++) {
        const bool state = TOPIC_KINDS[i].kind == TopicKind::STATE;
        table[i] = {TOPIC_KINDS[i].tag, TOPIC_KINDS[i].kind, state ? nothing : nullptr};
    }
    CHECK(servesEveryTopic(table));

    const size_t imu = 0, mode = 1;
    CHECK(table[mode].tag == MODE_TAG);
    table[mode].snapshot = nullptr;
    CHECK(!servesEveryTopic(table));
    table[mode].snapshot = nothing;

    table[imu].snapshot = nothing;
    CHECK(!servesEveryTopic(table));
    table[imu].snapshot = nullptr;

    table[mode].kind = TopicKind::STREAM;
    table[mode].snapshot = nullptr;
    CHECK(!servesEveryTopic(table));

    Topic missingOne[std::size(TOPIC_KINDS) - 1];
    std::copy(std::begin(table), std::end(table) - 1, std::begin(missingOne));
    CHECK(!servesEveryTopic(missingOne));
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s platform_shared/topics.json\n", argv[0]);
        return 2;
    }
    aSubscribeToAStateTopicIsAnsweredWithTheCurrentValueThenTheAck();
    aSecondClientGetsItsOwnSnapshotAndTheFirstNothing();
    aRepeatedSubscribeIsAnsweredAgainAndGetsEachPushOnce();
    oneUnsubscribeEndsARepeatedSubscription();
    aStreamAndAnUnknownTagGetTheAckAlone();
    theSnapshotIsTheValueAtTheSubscribeAndALaterPushFollowsTheAck();
    aChangeMadeWhileTheSnapshotIsSentIsPushedAfterIt();
    aDroppedStatePushIsResentOnceAsTheCurrentValue();
    aDroppedEmitOfAStateTopicIsResentToo();
    aStatePushWhoseSendFailsIsResentOnceItGoesOut();
    aSubscribeWhoseSnapshotFailsIsLeftUnackedAndResent();
    aSubscribeFromAClientThatCannotTakeAFrameIsAnsweredLater();
    aDroppedStreamFrameAndADeliveredPushAreNotResent();
    aClientThatLeavesIsResentNothing();
    aPushGoesOnlyToAStateTopicsSubscribers();
    aSubscribesListenerSendsBeforeTheAck();
    theTopicTableMatchesTheSharedOne(argv[1]);
    aTableServesEveryTopicOnlyWithEachTopicAndASnapshotForEachState();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
