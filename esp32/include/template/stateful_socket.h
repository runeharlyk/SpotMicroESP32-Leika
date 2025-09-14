#pragma once

#include <PsychicHttp.h>
#include <string>

// #include <communication/websocket_adapter.h>
#include <template/stateful_service.h>

template <class T>
class EventEndpoint {
  public:
    EventEndpoint(JsonStateReader<T> stateReader, JsonStateUpdater<T> stateUpdater, StatefulService<T> *statefulService,
                  const char *event)
        : _stateReader(stateReader), _stateUpdater(stateUpdater), _statefulService(statefulService), _event(event) {
        _statefulService->addUpdateHandler([&](const std::string &originId) { syncState(originId); }, false);
    }

    void begin() {
        // socket.onEvent(_event,
        //                std::bind(&EventEndpoint::updateState, this, std::placeholders::_1, std::placeholders::_2));
        // socket.onSubscribe(_event,
        //                    std::bind(&EventEndpoint::syncState, this, std::placeholders::_1, std::placeholders::_2));
    }

  private:
    JsonStateReader<T> _stateReader;
    JsonStateUpdater<T> _stateUpdater;
    StatefulService<T> *_statefulService;
    const char *_event;

    void updateState(JsonVariant &root, int originId) {
        _statefulService->update(root, _stateUpdater, std::to_string(originId));
    }

    void syncState(const std::string &originId, bool sync = false) {
        JsonDocument jsonDocument;
        JsonVariant root = jsonDocument.to<JsonVariant>();
        _statefulService->read(root, _stateReader);
        // socket.emit(_event, root, originId.c_str(), sync);
    }
};
