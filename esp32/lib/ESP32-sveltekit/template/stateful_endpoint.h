#pragma once

#include <PsychicHttp.h>
#include <template/stateful_service.h>

#include <functional>

#define HTTP_ENDPOINT_ORIGIN_ID "http"
#define HTTPS_ENDPOINT_ORIGIN_ID "https"

template <class T>
class StatefulHttpEndpoint {
  protected:
    JsonStateReader<T> _stateReader;
    JsonStateUpdater<T> _stateUpdater;
    StatefulService<T> *_statefulService;

  public:
    StatefulHttpEndpoint(JsonStateReader<T> stateReader, JsonStateUpdater<T> stateUpdater,
                         StatefulService<T> *statefulService)
        : _stateReader(stateReader), _stateUpdater(stateUpdater), _statefulService(statefulService) {}

    esp_err_t handleStateUpdate(PsychicRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) return request->reply(400);

        JsonObject jsonObject = json.as<JsonObject>();
        StateUpdateResult outcome = _statefulService->updateWithoutPropagation(jsonObject, _stateUpdater);

        if (outcome == StateUpdateResult::ERROR)
            return request->reply(400);
        else if ((outcome == StateUpdateResult::CHANGED)) {
            // persist the changes to the FS
            _statefulService->callUpdateHandlers(HTTP_ENDPOINT_ORIGIN_ID);
        }

        PsychicJsonResponse response = PsychicJsonResponse(request, false);
        jsonObject = response.getRoot();

        _statefulService->read(jsonObject, _stateReader);

        return response.send();
    }

    esp_err_t getState(PsychicRequest *request) {
        PsychicJsonResponse response = PsychicJsonResponse(request, false);
        JsonObject jsonObject = response.getRoot();
        _statefulService->read(jsonObject, _stateReader);
        return response.send();
    }
};
