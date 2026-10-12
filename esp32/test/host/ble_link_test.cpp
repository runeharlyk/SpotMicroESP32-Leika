// Host test of BleLink, the Bluetooth link's protocol above the radio, built and run by test_host_programs.py.
#include <cstdio>
#include <cstring>
#include <vector>
#include <communication/ble_link.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

using Bytes = std::vector<uint8_t>;

// A radio with a central that writes chunks of `mtuPayload` bytes and records the notifications it gets.
struct FakeRadio : BleLink {
    size_t mtuPayload = 20;
    std::vector<Bytes> notifications;
    int failFromNotification = -1; // the index of the first notification that fails, or -1
    int drops = 0;

    void connect() { onConnected(); }
    void receivedChunk(const Bytes &chunk) { received(chunk.data(), chunk.size()); }
    void disconnect() {
        onDisconnected();
        resetReceiver();
    }

    // What the app does: frames the message, writes it chunk by chunk.
    void write(const socket_message_Message &message) {
        uint8_t buffer[512];
        pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
        pb_encode(&stream, socket_message_Message_fields, &message);
        for (const Bytes &chunk : ble_frame::encode(buffer, stream.bytes_written, mtuPayload))
            received(chunk.data(), chunk.size());
    }

    // What the app does with the notifications: rebuilds the messages and decodes them.
    std::vector<socket_message_Message> heard() {
        std::vector<socket_message_Message> messages;
        ble_frame::Decoder decoder([&](const uint8_t *m, size_t n) {
            socket_message_Message message = socket_message_Message_init_zero;
            pb_istream_t stream = pb_istream_from_buffer(m, n);
            if (pb_decode(&stream, socket_message_Message_fields, &message)) messages.push_back(message);
        });
        for (const Bytes &chunk : notifications) decoder.feed(chunk.data(), chunk.size());
        return messages;
    }

  protected:
    size_t chunkSize() override { return mtuPayload; }
    bool notify(const uint8_t *chunk, size_t len) override {
        if (failFromNotification >= 0 && static_cast<int>(notifications.size()) >= failFromNotification) return false;
        notifications.emplace_back(chunk, chunk + len);
        return true;
    }
    void dropConnection() override { drops++; }
};

static socket_message_Message request(uint32_t correlationId) {
    socket_message_Message message = socket_message_Message_init_zero;
    message.which_message = socket_message_Message_correlation_request_tag;
    message.message.correlation_request.correlation_id = correlationId;
    message.message.correlation_request.which_request = socket_message_CorrelationRequest_features_data_request_tag;
    return message;
}

// Answers as main.cpp does, with a reply long enough to span several notifications.
static void answerRequests(FakeRadio &radio) {
    radio.on<socket_message_CorrelationRequest>([&radio](const socket_message_CorrelationRequest &req, int cid) {
        socket_message_CorrelationResponse res = socket_message_CorrelationResponse_init_zero;
        res.correlation_id = req.correlation_id;
        res.status_code = 404;
        std::memset(res.error_message, 'x', 60);
        radio.emit(res, cid);
    });
}

static void aRequestWrittenInChunksIsAnsweredInNotifications() {
    FakeRadio radio;
    answerRequests(radio);
    radio.connect();

    radio.write(request(41));

    const auto heard = radio.heard();
    CHECK(radio.notifications.size() > 2);
    for (const Bytes &chunk : radio.notifications) CHECK(chunk.size() <= radio.mtuPayload);
    CHECK(heard.size() == 1);
    CHECK(heard[0].which_message == socket_message_Message_correlation_response_tag);
    CHECK(heard[0].message.correlation_response.correlation_id == 41);
    CHECK(heard[0].message.correlation_response.status_code == 404);
    CHECK(std::strlen(heard[0].message.correlation_response.error_message) == 60);
}

static void aPingIsAnsweredWithAPong() {
    FakeRadio radio;
    radio.connect();
    socket_message_Message ping = socket_message_Message_init_zero;
    ping.which_message = socket_message_Message_pingmsg_tag;

    radio.write(ping);

    const auto heard = radio.heard();
    CHECK(heard.size() == 1 && heard[0].which_message == socket_message_Message_pongmsg_tag);
}

// The rest of a cut-off message would be read as the start of the next one, so the link must start over.
static void aMessageCutOffMidwayDropsTheConnection() {
    FakeRadio radio;
    answerRequests(radio);
    radio.connect();
    radio.failFromNotification = 1;

    radio.write(request(1));

    CHECK(radio.drops == 1);
}

static void aMessageThatCouldNotStartKeepsTheConnection() {
    FakeRadio radio;
    answerRequests(radio);
    radio.connect();
    radio.failFromNotification = 0;

    radio.write(request(1));

    CHECK(radio.drops == 0);
}

static void aLateReplyDoesNotReachTheNextConnection() {
    FakeRadio radio;
    radio.connect();
    const uint32_t session = radio.session(0);
    radio.disconnect();
    radio.connect();

    socket_message_CorrelationResponse res = socket_message_CorrelationResponse_init_zero;
    radio.emitToSession(res, 0, session);

    CHECK(radio.notifications.empty());
}

static void subscriptionsEndWithTheConnection() {
    FakeRadio radio;
    radio.connect();
    socket_message_Message subscribe = socket_message_Message_init_zero;
    subscribe.which_message = socket_message_Message_sub_notif_tag;
    subscribe.message.sub_notif.tag = socket_message_Message_imu_tag;
    radio.write(subscribe);
    socket_message_IMUData imu = socket_message_IMUData_init_zero;
    imu.x = 1;

    radio.emit(imu);
    CHECK(radio.heard().size() == 1);

    radio.disconnect();
    radio.connect();
    radio.notifications.clear();
    radio.emit(imu);
    CHECK(radio.notifications.empty());
}

// A connection that ended mid-write must not leave its half message in front of the next connection's first one.
static void aHalfWrittenMessageIsForgottenWithItsConnection() {
    FakeRadio radio;
    answerRequests(radio);
    radio.connect();
    const Bytes half = {30, 0, 0x08};
    radio.receivedChunk(half);
    radio.disconnect();
    radio.connect();

    radio.write(request(7));

    const auto heard = radio.heard();
    CHECK(heard.size() == 1 && heard[0].message.correlation_response.correlation_id == 7);
}

int main() {
    aRequestWrittenInChunksIsAnsweredInNotifications();
    aPingIsAnsweredWithAPong();
    aMessageCutOffMidwayDropsTheConnection();
    aMessageThatCouldNotStartKeepsTheConnection();
    aLateReplyDoesNotReachTheNextConnection();
    subscriptionsEndWithTheConnection();
    aHalfWrittenMessageIsForgottenWithItsConnection();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
