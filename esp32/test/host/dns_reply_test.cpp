// Host test of wifi/dns_reply.h, built and run by test_host_programs.py.
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include <wifi/dns_reply.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

constexpr uint16_t TYPE_A = 1;
constexpr uint16_t TYPE_AAAA = 28;
static const uint8_t PORTAL[4] = {192, 168, 4, 1};

// A query as a phone's resolver sends it, optionally with the EDNS record most resolvers add.
static std::vector<uint8_t> query(const char *name, uint16_t type, bool edns = false) {
    std::vector<uint8_t> packet = {0xAB, 0xCD, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, edns ? uint8_t(1) : uint8_t(0)};
    for (const char *label = name; *label;) {
        const char *end = std::strchr(label, '.');
        size_t length = end ? size_t(end - label) : std::strlen(label);
        packet.push_back(uint8_t(length));
        packet.insert(packet.end(), label, label + length);
        label += length + (end ? 1 : 0);
    }
    packet.insert(packet.end(), {0x00, uint8_t(type >> 8), uint8_t(type), 0x00, 0x01});
    if (edns) packet.insert(packet.end(), {0x00, 0x00, 0x29, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    return packet;
}

static uint16_t field(const uint8_t *packet, size_t offset) { return uint16_t(packet[offset] << 8 | packet[offset + 1]); }

static size_t reply(const std::vector<uint8_t> &request, uint8_t *out, size_t size = DNS_MAX_PACKET_SIZE) {
    return dnsReply(request.data(), request.size(), PORTAL, out, size);
}

// Every name resolves to the robot, which is what makes phones open the portal page.
static void anAddressQueryIsAnsweredWithThePortal() {
    const std::vector<uint8_t> request = query("connectivitycheck.gstatic.com", TYPE_A);
    uint8_t out[DNS_MAX_PACKET_SIZE];
    const size_t length = reply(request, out);
    CHECK(length == request.size() + 16);
    CHECK(field(out, 0) == 0xABCD);
    CHECK((out[2] & 0x80) != 0);
    CHECK((out[3] & 0x0F) == 0);
    CHECK(field(out, 4) == 1);
    CHECK(field(out, 6) == 1);
    CHECK(field(out, 8) == 0);
    CHECK(field(out, 10) == 0);
    CHECK(std::memcmp(out + 12, request.data() + 12, request.size() - 12) == 0);
    CHECK(std::memcmp(out + length - 4, PORTAL, 4) == 0);
}

// The answer must follow the question: after the EDNS record, a resolver would read that record as it.
static void theAnswerFollowsTheQuestionWhenTheQueryCarriesEdns() {
    const std::vector<uint8_t> plain = query("example.com", TYPE_A);
    const std::vector<uint8_t> request = query("example.com", TYPE_A, true);
    uint8_t out[DNS_MAX_PACKET_SIZE];
    const size_t length = reply(request, out);
    CHECK(length == plain.size() + 16);
    CHECK(field(out, 10) == 0);
    CHECK(std::memcmp(out + length - 4, PORTAL, 4) == 0);
}

// An IPv4 address is no answer to an IPv6 query; an empty one sends the phone back to asking for IPv4.
static void anIpv6QueryGetsAnEmptyAnswer() {
    const std::vector<uint8_t> request = query("example.com", TYPE_AAAA);
    uint8_t out[DNS_MAX_PACKET_SIZE];
    const size_t length = reply(request, out);
    CHECK(length == request.size());
    CHECK((out[2] & 0x80) != 0);
    CHECK(field(out, 6) == 0);
}

static void responsesAndMalformedPacketsGetNoReply() {
    uint8_t out[DNS_MAX_PACKET_SIZE];
    std::vector<uint8_t> response = query("example.com", TYPE_A);
    response[2] |= 0x80;
    CHECK(reply(response, out) == 0);

    std::vector<uint8_t> runsPastTheEnd = query("example.com", TYPE_A);
    runsPastTheEnd.resize(runsPastTheEnd.size() - 3);
    CHECK(reply(runsPastTheEnd, out) == 0);

    std::vector<uint8_t> twoQuestions = query("example.com", TYPE_A);
    twoQuestions[5] = 2;
    CHECK(reply(twoQuestions, out) == 0);

    std::vector<uint8_t> compressedName = query("example.com", TYPE_A);
    compressedName[12] = 0xC0;
    CHECK(reply(compressedName, out) == 0);

    CHECK(reply(std::vector<uint8_t>(11, 0), out) == 0);
}

// Anyone who joins the access point can send anything: no packet may make the reply overrun its buffer.
static void noPacketWritesPastTheReplyBuffer() {
    struct Guarded {
        uint8_t out[DNS_MAX_PACKET_SIZE];
        uint8_t guard[64];
    } buffer;
    std::mt19937 random(7);
    const std::vector<uint8_t> longest = [] {
        std::vector<uint8_t> packet = query("a", TYPE_A);
        packet.resize(12);
        while (packet.size() < DNS_MAX_PACKET_SIZE - 6) {
            packet.push_back(63);
            packet.insert(packet.end(), 63, 'x');
        }
        packet.resize(DNS_MAX_PACKET_SIZE - 5);
        packet.insert(packet.end(), {0x00, 0x00, 0x01, 0x00, 0x01});
        return packet;
    }();
    for (int round = 0; round < 20000; round++) {
        std::vector<uint8_t> packet = round == 0 ? longest : query("portal.example.com", TYPE_A, round % 2);
        if (round > 0) {
            packet.resize(random() % (DNS_MAX_PACKET_SIZE + 1));
            for (uint8_t &byte : packet) {
                if (random() % 4 == 0) byte = uint8_t(random());
            }
            if (packet.size() > 2) packet[2] &= 0x7F;
        }
        std::memset(buffer.guard, 0x5A, sizeof(buffer.guard));
        const size_t length = reply(packet, buffer.out, sizeof(buffer.out));
        CHECK(length <= sizeof(buffer.out));
        bool guardIntact = true;
        for (uint8_t byte : buffer.guard) guardIntact &= byte == 0x5A;
        CHECK(guardIntact);
        if (!guardIntact) break;
    }
}

int main() {
    anAddressQueryIsAnsweredWithThePortal();
    theAnswerFollowsTheQuestionWhenTheQueryCarriesEdns();
    anIpv6QueryGetsAnEmptyAnswer();
    responsesAndMalformedPacketsGetNoReply();
    noPacketWritesPastTheReplyBuffer();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
