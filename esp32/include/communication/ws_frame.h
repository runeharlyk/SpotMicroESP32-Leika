#pragma once

#include <cstddef>
#include <cstdint>

/** The header of a WebSocket frame from the server (RFC 6455 section 5.2), written apart from its payload. */
namespace ws_frame {

constexpr size_t MAX_HEADER = 10;

/** Writes the header of an unmasked, final binary frame of `length` bytes into `out` (MAX_HEADER bytes or more);
 * returns its size. */
inline size_t binaryHeader(uint8_t *out, size_t length) {
    out[0] = 0x82;
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

}  // namespace ws_frame
