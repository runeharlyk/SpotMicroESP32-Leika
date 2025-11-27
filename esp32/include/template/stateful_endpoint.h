#pragma once

#include <esp_http_server.h>
#include <template/stateful_service.h>
#include <utils/http_utils.h>

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

    esp_err_t handleStateUpdate(httpd_req_t *req, JsonVariant &json) {
        JsonVariant jsonObject = json.as<JsonVariant>();
        StateUpdateResult outcome = _statefulService->updateWithoutPropagation(jsonObject, _stateUpdater);

        if (outcome == StateUpdateResult::ERROR) {
            return http_utils::send_error(req, 400);
        } else if ((outcome == StateUpdateResult::CHANGED)) {
            _statefulService->callUpdateHandlers(HTTP_ENDPOINT_ORIGIN_ID);
        }

        JsonDocument doc;
        jsonObject = doc.to<JsonVariant>();
        _statefulService->read(jsonObject, _stateReader);

        return http_utils::send_json_response(req, doc);
    }

    esp_err_t getState(httpd_req_t *req) {
        JsonDocument doc;
        JsonVariant jsonObject = doc.to<JsonVariant>();
        _statefulService->read(jsonObject, _stateReader);
        return http_utils::send_json_response(req, doc);
    }
};
