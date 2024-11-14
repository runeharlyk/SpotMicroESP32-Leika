#pragma once

#include <PsychicHttp.h>

#include <event_socket.h>
#include <template/stateful_service.h>

template <class T>
class EventEndpoint {
  public:
    EventEndpoint(JsonStateReader<T> stateReader, JsonStateUpdater<T> stateUpdater, StatefulService<T> *statefulService,
                  const char *event)
        : _stateReader(stateReader), _stateUpdater(stateUpdater), _statefulService(statefulService), _event(event) {
        _statefulService->addUpdateHandler([&](const String &originId) { syncState(originId); }, false);
    }

    void begin() {
        socket.onEvent(_event,
                       std::bind(&EventEndpoint::updateState, this, std::placeholders::_1, std::placeholders::_2));
        socket.onSubscribe(_event,
                           std::bind(&EventEndpoint::syncState, this, std::placeholders::_1, std::placeholders::_2));
    }

  private:
    JsonStateReader<T> _stateReader;
    JsonStateUpdater<T> _stateUpdater;
    StatefulService<T> *_statefulService;
    const char *_event;

    void updateState(JsonObject &root, int originId) {
        _statefulService->update(root, _stateUpdater, String(originId));
    }

    void syncState(const String &originId, bool sync = false) {
        JsonDocument jsonDocument;
        JsonObject root = jsonDocument.to<JsonObject>();
        String output;
        _statefulService->read(root, _stateReader);
        serializeJson(root, output);
        ESP_LOGV("EventEndpoint", "Syncing state: %s", output.c_str());
        socket.emit(_event, output.c_str(), originId.c_str(), sync);
    }
};
