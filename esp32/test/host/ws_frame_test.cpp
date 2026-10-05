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

int main() {
    shortPayloadsCarryTheirLengthInTheSecondByte();
    mediumPayloadsUseASixteenBitLength();
    largePayloadsUseASixtyFourBitLength();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
