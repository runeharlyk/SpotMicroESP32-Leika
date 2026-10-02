#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#define DNS_MAX_PACKET_SIZE 512

/**
 * The captive portal's answer to one DNS query: every name resolves to `address`, which is what makes
 * a phone that joins the access point open the robot's page. Returns the reply's length in `out`, or 0
 * when the packet gets no reply (a response, or not one well-formed question). Anyone on the access
 * point can send anything, so the question is parsed, never trusted, and the reply never exceeds `size`.
 */
inline size_t dnsReply(const uint8_t *query, size_t length, const uint8_t address[4], uint8_t *out, size_t size) {
    constexpr size_t HEADER = 12;
    constexpr uint16_t TYPE_A = 1;
    constexpr uint16_t CLASS_IN = 1;
    if (length < HEADER) return 0;
    const bool isResponse = query[2] & 0x80;
    const uint16_t questions = uint16_t(query[4] << 8 | query[5]);
    if (isResponse || questions != 1) return 0;

    // The name is labels of 1 to 63 bytes up to an empty one; a query holds no compression pointers.
    size_t end = HEADER;
    while (end < length && query[end] != 0) {
        if (query[end] > 63) return 0;
        end += 1 + query[end];
    }
    end += 1 + 4;
    if (end > length) return 0;
    const uint16_t type = uint16_t(query[end - 4] << 8 | query[end - 3]);
    const uint16_t qclass = uint16_t(query[end - 2] << 8 | query[end - 1]);
    const bool answered = type == TYPE_A && qclass == CLASS_IN;

    // The question is echoed and the answer follows it, so any records after the question, such as EDNS, go.
    const uint8_t answer[] = {0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x04};
    const size_t replyLength = end + (answered ? sizeof(answer) + 4 : 0);
    if (replyLength > size) return 0;
    memcpy(out, query, end);
    out[2] = 0x80 | (query[2] & 0x01) | 0x04;
    out[3] = 0x80;
    out[6] = 0;
    out[7] = answered ? 1 : 0;
    memset(out + 8, 0, 4);
    if (answered) {
        memcpy(out + end, answer, sizeof(answer));
        memcpy(out + end + sizeof(answer), address, 4);
    }
    return replyLength;
}
