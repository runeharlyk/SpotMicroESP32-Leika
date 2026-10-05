#pragma once

#include <cstddef>
#include <cstdint>

/** WebSocket framing the server does itself (RFC 6455): binary frame headers, and the answers to control frames. */
namespace ws_frame {

constexpr size_t MAX_HEADER = 10;

/** Frame opcodes; httpd's HTTPD_WS_TYPE_* use the same values. */
enum Opcode : uint8_t { NONE = 0x0, BINARY = 0x2, CLOSE = 0x8, PING = 0x9, PONG = 0xA };

/** Writes the header of an unmasked, final binary frame of `length` bytes into `out` (MAX_HEADER bytes or more);
 * returns its size. */
inline size_t binaryHeader(uint8_t *out, size_t length) {
    out[0] = 0x80 | BINARY;
    if (length < 126) {
        out[1] = static_cast<uint8_t>(length);
        return 2;
    }
    if (length <= 0xffff) {
        out[1] = 126;
        out[2] = static_cast<uint8_t>(length >> 8);
        out[3] = static_cast<uint8_t>(length);
        return 4;
    }
    out[1] = 127;
    const uint64_t wide = length;
    for (int i = 0; i < 8; i++) out[2 + i] = static_cast<uint8_t>(wide >> (8 * (7 - i)));
    return MAX_HEADER;
}

/** What to send back for a received frame; opcode NONE when nothing is due. */
struct Reply {
    Opcode opcode;
    const uint8_t *payload;
    size_t len;
    bool endsSession;
};

/**
 * The answer a control frame is owed (section 5.5): a close is echoed with its status code, the first two bytes of its
 * payload, after which the session ends; a ping is answered with a pong carrying its payload.
 */
inline Reply controlReply(Opcode received, const uint8_t *payload, size_t len) {
    switch (received) {
        case CLOSE: return {CLOSE, payload, len < 2 ? size_t {0} : size_t {2}, true};
        case PING: return {PONG, payload, len, false};
        default: return {NONE, nullptr, 0, false};
    }
}

}  // namespace ws_frame
