// Host test of ota_session.h, built and run by test_host_programs.py.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <ota_session.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// The next app slot, recording what the session did to it; each step can be made to fail.
struct FakeFlash : OtaFlash {
    uint32_t slot = 1024;
    bool beginOk = true, writeOk = true, endOk = true, activateOk = true;
    std::vector<uint8_t> written;
    int begins = 0, ends = 0, aborts = 0;
    bool open = false, active = false;

    uint32_t capacity() override { return slot; }
    bool begin() override {
        begins++;
        open = beginOk;
        written.clear();
        return beginOk;
    }
    bool write(const uint8_t *data, size_t size) override {
        if (!writeOk) return false;
        written.insert(written.end(), data, data + size);
        return true;
    }
    bool end() override {
        ends++;
        open = false;
        return endOk;
    }
    bool activate() override {
        active = activateOk;
        return activateOk;
    }
    void abort() override {
        aborts++;
        open = false;
    }
};

static const int APP = 7;
static const int OTHER = 8;
static const uint8_t BYTES[8] = {1, 2, 3, 4, 5, 6, 7, 8};

static bool ok(OtaReply reply) { return reply.status == 200; }

static void aWholeImageIsWrittenAndSelected() {
    FakeFlash flash;
    OtaSession session(flash);
    CHECK(ok(session.start(8, true, APP, 0)));
    CHECK(session.running());
    CHECK(ok(session.chunk(0, BYTES, 5, APP, 10)));
    CHECK(ok(session.chunk(1, BYTES + 5, 3, APP, 20)));
    CHECK(ok(session.finish(APP)));
    CHECK(flash.written == std::vector<uint8_t>(BYTES, BYTES + 8));
    CHECK(flash.ends == 1 && flash.active && flash.aborts == 0);
    CHECK(!session.running());
}

static void onlyADeactivatedRobotIsUpdated() {
    FakeFlash flash;
    OtaSession session(flash);
    OtaReply reply = session.start(8, false, APP, 0);
    CHECK(reply.status == 409 && std::strcmp(reply.reason, "Deactivate first") == 0);
    CHECK(flash.begins == 0 && !session.running());
}

static void oneUpdateAtATime() {
    FakeFlash flash;
    OtaSession session(flash);
    CHECK(ok(session.start(8, true, APP, 0)));
    CHECK(session.start(8, true, OTHER, 0).status == 409);
    CHECK(flash.begins == 1);
    CHECK(session.chunk(0, BYTES, 8, OTHER, 0).status == 409);
    CHECK(session.finish(OTHER).status == 409);
    CHECK(flash.written.empty() && session.running());
}

static void anImageLargerThanTheSlotIsRefused() {
    FakeFlash flash;
    OtaSession session(flash);
    CHECK(session.start(flash.slot + 1, true, APP, 0).status == 413);
    CHECK(session.start(0, true, APP, 0).status == 413);
    CHECK(flash.begins == 0);
    flash.slot = 0;
    CHECK(session.start(8, true, APP, 0).status == 501);
}

static void aSlotThatWillNotOpenFailsTheStart() {
    FakeFlash flash;
    flash.beginOk = false;
    OtaSession session(flash);
    CHECK(session.start(8, true, APP, 0).status == 500);
    CHECK(!session.running());
}

static void aGapAbortsAndTheBootImageStays() {
    FakeFlash flash;
    OtaSession session(flash);
    session.start(8, true, APP, 0);
    session.chunk(0, BYTES, 4, APP, 0);
    OtaReply reply = session.chunk(2, BYTES + 4, 4, APP, 0);
    CHECK(reply.status == 400 && std::strcmp(reply.reason, "A chunk arrived out of order") == 0);
    CHECK(flash.aborts == 1 && !session.running() && !flash.active);
    CHECK(session.finish(APP).status == 409);
    CHECK(flash.ends == 0);
}

static void moreDataThanAnnouncedAborts() {
    FakeFlash flash;
    OtaSession session(flash);
    session.start(6, true, APP, 0);
    CHECK(session.chunk(0, BYTES, 8, APP, 0).status == 400);
    CHECK(flash.aborts == 1 && flash.written.empty() && !session.running());
}

static void aFailedWriteAborts() {
    FakeFlash flash;
    OtaSession session(flash);
    session.start(8, true, APP, 0);
    flash.writeOk = false;
    CHECK(session.chunk(0, BYTES, 8, APP, 0).status == 500);
    CHECK(flash.aborts == 1 && !session.running());
}

static void anIncompleteImageIsNotSelected() {
    FakeFlash flash;
    OtaSession session(flash);
    session.start(8, true, APP, 0);
    session.chunk(0, BYTES, 4, APP, 0);
    CHECK(session.finish(APP).status == 400);
    CHECK(flash.aborts == 1 && flash.ends == 0 && !flash.active && !session.running());
}

static void aRejectedImageIsNotSelected() {
    FakeFlash flash;
    flash.endOk = false;
    OtaSession session(flash);
    session.start(8, true, APP, 0);
    session.chunk(0, BYTES, 8, APP, 0);
    CHECK(session.finish(APP).status == 422);
    CHECK(!flash.active && !session.running());
    // esp_ota_end releases its handle whatever it returns, so there is nothing left to abort.
    CHECK(flash.aborts == 0);
}

static void aBootPartitionThatWillNotSwitchIsReported() {
    FakeFlash flash;
    flash.activateOk = false;
    OtaSession session(flash);
    session.start(8, true, APP, 0);
    session.chunk(0, BYTES, 8, APP, 0);
    CHECK(session.finish(APP).status == 500);
    CHECK(!session.running());
}

static void theOwnerLeavingAborts() {
    FakeFlash flash;
    OtaSession session(flash);
    session.start(8, true, APP, 0);
    session.drop(OTHER);
    CHECK(session.running() && flash.aborts == 0);
    session.drop(APP);
    CHECK(!session.running() && flash.aborts == 1);
    session.drop(APP);
    CHECK(flash.aborts == 1);
}

static void silenceAbortsAfterTheTimeout() {
    FakeFlash flash;
    OtaSession session(flash);
    session.start(8, true, APP, 1000);
    session.chunk(0, BYTES, 4, APP, 5000);
    session.expire(5000 + OtaSession::TIMEOUT_MS);
    CHECK(session.running());
    session.expire(5001 + OtaSession::TIMEOUT_MS);
    CHECK(!session.running() && flash.aborts == 1);
    CHECK(session.chunk(1, BYTES + 4, 4, APP, 0).status == 409);
}

static void anAbortedUpdateCanBeStartedAgain() {
    FakeFlash flash;
    OtaSession session(flash);
    session.start(8, true, APP, 0);
    session.drop(APP);
    CHECK(ok(session.start(8, true, OTHER, 0)));
    CHECK(ok(session.chunk(0, BYTES, 8, OTHER, 0)));
    CHECK(ok(session.finish(OTHER)));
    CHECK(flash.written.size() == 8 && flash.active);
}

int main() {
    aWholeImageIsWrittenAndSelected();
    onlyADeactivatedRobotIsUpdated();
    oneUpdateAtATime();
    anImageLargerThanTheSlotIsRefused();
    aSlotThatWillNotOpenFailsTheStart();
    aGapAbortsAndTheBootImageStays();
    moreDataThanAnnouncedAborts();
    aFailedWriteAborts();
    anIncompleteImageIsNotSelected();
    aRejectedImageIsNotSelected();
    aBootPartitionThatWillNotSwitchIsReported();
    theOwnerLeavingAborts();
    silenceAbortsAfterTheTimeout();
    anAbortedUpdateCanBeStartedAgain();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
