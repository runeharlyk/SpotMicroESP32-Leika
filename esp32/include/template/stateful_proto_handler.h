#pragma once

#include <template/stateful_service.h>

#include <functional>

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
  public:
    /** Converts internal state to the protobuf message */
    using ProtoStateReader = std::function<void(const T&, ProtoT&)>;
    /** Applies an incoming protobuf message to the internal state */
    using ProtoStateUpdater = std::function<StateUpdateResult(const ProtoT&, T&)>;

    StatefulProtoHandler(ProtoStateReader stateReader, ProtoStateUpdater stateUpdater,
                         StatefulService<T>* statefulService)
        : _stateReader(stateReader), _stateUpdater(stateUpdater), _statefulService(statefulService) {}

    void read(ProtoT& proto) {
        _statefulService->read([this, &proto](const T& settings) { _stateReader(settings, proto); });
    }

    StateUpdateResult update(const ProtoT& proto) {
        return _statefulService->update([this, &proto](T& settings) { return _stateUpdater(proto, settings); },
                                        PROTO_HANDLER_ORIGIN_ID);
    }

  private:
    ProtoStateReader _stateReader;
    ProtoStateUpdater _stateUpdater;
    StatefulService<T>* _statefulService;
};
