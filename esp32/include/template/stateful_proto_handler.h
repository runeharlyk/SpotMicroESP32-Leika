#pragma once

#include <template/stateful_service.h>
#include <pb_encode.h>

#include <functional>
#include <type_traits>
#include <vector>

#define PROTO_HANDLER_ORIGIN_ID "proto"

/**
 * Reads and updates a stateful service through its protobuf message, independent of the transport
 * that carries the message (the socket's correlation requests).
 *
 * @tparam T The internal state type (e.g., APSettings C++ class)
 * @tparam ProtoT The protobuf message type (e.g., api_APSettings)
 */
template <class T, class ProtoT>
class StatefulProtoHandler {
    static_assert(std::is_same_v<T, ProtoT>, "the state is compared through its own proto encoding");

  public:
    /** Converts internal state to the protobuf message */
    using ProtoStateReader = std::function<void(const T&, ProtoT&)>;
    /** Applies an incoming protobuf message to the internal state */
    using ProtoStateUpdater = std::function<StateUpdateResult(const ProtoT&, T&)>;

    StatefulProtoHandler(ProtoStateReader stateReader, ProtoStateUpdater stateUpdater,
                         StatefulService<T>* statefulService, const pb_msgdesc_t* fields)
        : _stateReader(stateReader), _stateUpdater(stateUpdater), _statefulService(statefulService), _fields(fields) {}

    void read(ProtoT& proto) {
        _statefulService->read([this, &proto](const T& settings) { _stateReader(settings, proto); });
    }

    // The app saves whole forms, so most saves repeat what is stored: an update that leaves the encoded
    // state as it was is UNCHANGED, and neither rewrites flash nor reconfigures anything.
    StateUpdateResult update(const ProtoT& proto) {
        return _statefulService->update(
            [this, &proto](T& settings) {
                const std::vector<uint8_t> before = encoded(settings);
                StateUpdateResult result = _stateUpdater(proto, settings);
                if (result == StateUpdateResult::CHANGED && encoded(settings) == before) {
                    return StateUpdateResult::UNCHANGED;
                }
                return result;
            },
            PROTO_HANDLER_ORIGIN_ID);
    }

  private:
    // Encoded rather than compared in memory: struct padding is not part of the state.
    std::vector<uint8_t> encoded(const T& settings) {
        size_t size = 0;
        pb_get_encoded_size(&size, _fields, &settings);
        std::vector<uint8_t> bytes(size);
        pb_ostream_t stream = pb_ostream_from_buffer(bytes.data(), bytes.size());
        pb_encode(&stream, _fields, &settings);
        return bytes;
    }

    ProtoStateReader _stateReader;
    ProtoStateUpdater _stateUpdater;
    StatefulService<T>* _statefulService;
    const pb_msgdesc_t* _fields;
};
