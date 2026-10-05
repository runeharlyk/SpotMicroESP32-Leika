// Host test of ws_frame.h, built and run by test_host_programs.py.
#include <cstdio>
#include <vector>
#include <communication/ws_frame.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static std::vector<uint8_t> header(size_t length) {
    uint8_t out[ws_frame::MAX_HEADER];
    return std::vector<uint8_t>(out, out + ws_frame::binaryHeader(out, length));
}

// RFC 6455 section 5.2: FIN plus the binary opcode, no mask from a server, and the shortest length encoding.
static void shortPayloadsCarryTheirLengthInTheSecondByte() {
    CHECK(header(0) == std::vector<uint8_t>({0x82, 0x00}));
    CHECK(header(3) == std::vector<uint8_t>({0x82, 0x03}));
    CHECK(header(125) == std::vector<uint8_t>({0x82, 0x7d}));
}

static void mediumPayloadsUseASixteenBitLength() {
    CHECK(header(126) == std::vector<uint8_t>({0x82, 0x7e, 0x00, 0x7e}));
    CHECK(header(16400) == std::vector<uint8_t>({0x82, 0x7e, 0x40, 0x10}));
    CHECK(header(65535) == std::vector<uint8_t>({0x82, 0x7e, 0xff, 0xff}));
}

static void largePayloadsUseASixtyFourBitLength() {
    CHECK(header(65536) == std::vector<uint8_t>({0x82, 0x7f, 0, 0, 0, 0, 0x00, 0x01, 0x00, 0x00}));
}

// RFC 6455 section 5.5: a close is answered with a close carrying its status code, a ping with a pong carrying its
// payload; a pong and data frames get no answer.
static void aCloseIsEchoedWithItsStatusCode() {
    const uint8_t normal[] = {0x03, 0xe8, 'b', 'y', 'e'};
    ws_frame::Reply reply = ws_frame::controlReply(ws_frame::CLOSE, normal, sizeof(normal));
    CHECK(reply.opcode == ws_frame::CLOSE && reply.len == 2 && reply.payload == normal && reply.endsSession);
    reply = ws_frame::controlReply(ws_frame::CLOSE, nullptr, 0);
    CHECK(reply.opcode == ws_frame::CLOSE && reply.len == 0 && reply.endsSession);
}

static void aPingIsAnsweredWithItsPayload() {
    const uint8_t data[] = {1, 2, 3};
    const ws_frame::Reply reply = ws_frame::controlReply(ws_frame::PING, data, sizeof(data));
    CHECK(reply.opcode == ws_frame::PONG && reply.payload == data && reply.len == 3 && !reply.endsSession);
}

static void otherFramesGetNoAnswer() {
    CHECK(ws_frame::controlReply(ws_frame::PONG, nullptr, 0).opcode == ws_frame::NONE);
    CHECK(ws_frame::controlReply(ws_frame::BINARY, nullptr, 0).opcode == ws_frame::NONE);
    CHECK(!ws_frame::controlReply(ws_frame::BINARY, nullptr, 0).endsSession);
}

int main() {
    aCloseIsEchoedWithItsStatusCode();
    aPingIsAnsweredWithItsPayload();
    otherFramesGetNoAnswer();
    shortPayloadsCarryTheirLengthInTheSecondByte();
    mediumPayloadsUseASixteenBitLength();
    largePayloadsUseASixtyFourBitLength();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
