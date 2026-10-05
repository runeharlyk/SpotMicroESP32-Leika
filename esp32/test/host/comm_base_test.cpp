// Host test of the subscription broadcasts in comm_base.hpp, built and run by test_host_programs.py.
#include <cstdio>
#include <set>
#include <vector>
#include <communication/comm_base.hpp>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// A link whose clients each have a socket that is ready or full, recording who was sent what.
struct FakeLink : CommAdapterBase {
    std::set<int> full;
    std::vector<int> sentTo;

    void join(int cid, int32_t tag) { subscribe(tag, cid); }

  protected:
    bool ready(int cid) override { return !full.count(cid); }
    bool send(const uint8_t *, size_t, int cid) override {
        sentTo.push_back(cid);
        return true;
    }
};

static socket_message_IMUData imu() { return socket_message_IMUData_init_zero; }

static void aStreamSkipsAClientThatCannotTakeIt() {
    FakeLink link;
    link.join(1, socket_message_Message_imu_tag);
    link.join(2, socket_message_Message_imu_tag);
    link.full.insert(2);

    CHECK(!link.emit(imu()));
    CHECK(link.sentTo == std::vector<int>({1}));
}

static void aSkippedClientGetsTheNextFrameOnceItCatchesUp() {
    FakeLink link;
    link.join(1, socket_message_Message_imu_tag);
    link.full.insert(1);
    link.emit(imu());
    link.full.clear();

    CHECK(link.emit(imu()));
    CHECK(link.sentTo == std::vector<int>({1}));
}

// A reply is the only answer its request gets, so it is sent whatever the socket's state.
static void aReplyIsSentEvenToAFullSocket() {
    FakeLink link;
    link.full.insert(3);
    socket_message_CorrelationResponse reply = socket_message_CorrelationResponse_init_zero;

    CHECK(link.emit(reply, 3));
    CHECK(link.sentTo == std::vector<int>({3}));
}

int main() {
    aStreamSkipsAClientThatCannotTakeIt();
    aSkippedClientGetsTheNextFrameOnceItCatchesUp();
    aReplyIsSentEvenToAFullSocket();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
