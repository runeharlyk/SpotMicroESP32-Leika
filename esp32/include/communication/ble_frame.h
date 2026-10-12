#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

/**
 * Protobuf messages over the Nordic UART Service, whose writes and notifications each hold at most one ATT payload.
 * A message goes out as a 2-byte little-endian length and the message, split into chunks of the payload size; every
 * message starts a new chunk. The app's ble-framing.ts implements the same, Hexapod's BLE link speaks it too, and
 * platform_shared/ble_frame_vectors.json pins both.
 */
namespace ble_frame {

constexpr size_t HEADER = 2;
/** The largest message the robot takes; the same as its encode buffer (PROTO_BUFFER_SIZE). */
constexpr size_t MAX_INCOMING = 4096;
/** The largest message the length prefix can describe. */
constexpr size_t MAX_MESSAGE = 0xFFFF;

/** The chunks to send for one message, in order; none for an empty message or one over MAX_MESSAGE. */
inline std::vector<std::vector<uint8_t>> encode(const uint8_t *message, size_t len, size_t chunkSize) {
    if (len == 0 || len > MAX_MESSAGE || chunkSize == 0) return {};
    std::vector<uint8_t> framed;
    framed.reserve(HEADER + len);
    framed.push_back(len & 0xFF);
    framed.push_back(len >> 8);
    framed.insert(framed.end(), message, message + len);

    std::vector<std::vector<uint8_t>> chunks;
    for (size_t at = 0; at < framed.size(); at += chunkSize) {
        const size_t end = at + chunkSize < framed.size() ? at + chunkSize : framed.size();
        chunks.emplace_back(framed.begin() + at, framed.begin() + end);
    }
    return chunks;
}

/**
 * Rebuilds messages from the chunks of one connection, fed as they arrive. A length of zero or over the limit can
 * only come from a lost chunk, so the buffered bytes are dropped; as every message starts a new chunk, the next one
 * starts afresh.
 */
class Decoder {
  public:
    using FrameHandler = std::function<void(const uint8_t *message, size_t len)>;

    explicit Decoder(FrameHandler onFrame, size_t maxMessage = MAX_INCOMING)
        : _onFrame(std::move(onFrame)), _maxMessage(maxMessage) {}

    void feed(const uint8_t *chunk, size_t len) {
        _buffer.insert(_buffer.end(), chunk, chunk + len);
        size_t at = 0;
        while (_buffer.size() - at >= HEADER) {
            const size_t messageLen = _buffer[at] | _buffer[at + 1] << 8;
            if (messageLen == 0 || messageLen > _maxMessage) {
                _buffer.clear();
                return;
            }
            if (_buffer.size() - at < HEADER + messageLen) break;
            _onFrame(_buffer.data() + at + HEADER, messageLen);
            at += HEADER + messageLen;
        }
        _buffer.erase(_buffer.begin(), _buffer.begin() + at);
    }

    /** Forgets a partial message, as when the connection it came on ends. */
    void reset() { _buffer.clear(); }

  private:
    FrameHandler _onFrame;
    size_t _maxMessage;
    std::vector<uint8_t> _buffer;
};

} // namespace ble_frame
