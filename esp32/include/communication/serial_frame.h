#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * Protobuf messages on a serial port that also carries the firmware's log. A frame is 0x00, COBS(version, message,
 * CRC-16), 0x00; log text never holds 0x00, so a reader tells the two apart byte for byte. The app's
 * serial-framing.ts implements the same, and platform_shared/serial_frame_vectors.json pins both.
 */
namespace serial_frame {

constexpr uint8_t VERSION = 1;
constexpr size_t MAX_MESSAGE = 4096;
// Version, message and CRC, plus a COBS code byte for every 254 bytes and one more.
constexpr size_t MAX_ENCODED = (1 + MAX_MESSAGE + 2) + (1 + MAX_MESSAGE + 2) / 254 + 1;

/** CRC-16/CCITT-FALSE: polynomial 0x1021, initial 0xFFFF, not reflected. */
inline uint16_t crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; bit++) crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}

/** The bytes to write for one message, delimiters included; empty for a message over MAX_MESSAGE. */
inline std::vector<uint8_t> encode(const uint8_t *message, size_t len) {
    if (len > MAX_MESSAGE) return {};
    std::vector<uint8_t> body;
    body.reserve(len + 3);
    body.push_back(VERSION);
    body.insert(body.end(), message, message + len);
    const uint16_t crc = crc16(body.data(), body.size());
    body.push_back(crc >> 8);
    body.push_back(crc & 0xFF);

    std::vector<uint8_t> out;
    out.reserve(body.size() + body.size() / 254 + 3);
    out.push_back(0);
    size_t codeAt = out.size();
    out.push_back(1);
    for (uint8_t byte : body) {
        if (byte != 0) {
            out.push_back(byte);
            out[codeAt]++;
        }
        if (byte == 0 || out[codeAt] == 0xFF) {
            codeAt = out.size();
            out.push_back(1);
        }
    }
    out.push_back(0);
    return out;
}

/**
 * Splits a byte stream, fed in pieces of any size, into messages and log lines. A segment that is not a valid frame is
 * log text, and the 0x00 that ended it starts the next frame: a reader that began mid-frame finds the frames again.
 */
class Decoder {
  public:
    using FrameHandler = std::function<void(const uint8_t *message, size_t len)>;
    using LineHandler = std::function<void(const std::string &line)>;

    Decoder(FrameHandler onFrame, LineHandler onLine) : _onFrame(std::move(onFrame)), _onLine(std::move(onLine)) {}

    void feed(const uint8_t *data, size_t len) {
        for (size_t i = 0; i < len; i++) feed(data[i]);
    }

  private:
    FrameHandler _onFrame;
    LineHandler _onLine;
    bool _inFrame = false;
    std::vector<uint8_t> _segment;
    std::string _line;

    void feed(uint8_t byte) {
        if (!_inFrame) {
            if (byte == 0) {
                flushLine();
                _inFrame = true;
                _segment.clear();
            } else {
                text(byte);
            }
            return;
        }
        if (byte != 0) {
            _segment.push_back(byte);
            if (_segment.size() > MAX_ENCODED) {
                for (uint8_t b : _segment) text(b);
                _segment.clear();
                _inFrame = false;
            }
            return;
        }
        if (_segment.empty()) return;
        if (deliver()) {
            _inFrame = false;
        } else {
            for (uint8_t b : _segment) text(b);
            flushLine();
        }
        _segment.clear();
    }

    void text(uint8_t byte) {
        if (byte != '\n') {
            _line.push_back(static_cast<char>(byte));
            return;
        }
        if (!_line.empty() && _line.back() == '\r') _line.pop_back();
        _onLine(_line);
        _line.clear();
    }

    void flushLine() {
        if (_line.empty()) return;
        if (_line.back() == '\r') _line.pop_back();
        _onLine(_line);
        _line.clear();
    }

    bool deliver() {
        std::vector<uint8_t> body;
        body.reserve(_segment.size());
        for (size_t i = 0; i < _segment.size();) {
            const uint8_t code = _segment[i++];
            if (code == 0 || i + code - 1 > _segment.size()) return false;
            body.insert(body.end(), _segment.begin() + i, _segment.begin() + i + code - 1);
            i += code - 1;
            if (code != 0xFF && i < _segment.size()) body.push_back(0);
        }
        if (body.size() < 3 || body[0] != VERSION || body.size() - 3 > MAX_MESSAGE) return false;
        const uint16_t crc = static_cast<uint16_t>(body[body.size() - 2] << 8 | body[body.size() - 1]);
        if (crc16(body.data(), body.size() - 2) != crc) return false;
        _onFrame(body.data() + 1, body.size() - 3);
        return true;
    }
};

} // namespace serial_frame
