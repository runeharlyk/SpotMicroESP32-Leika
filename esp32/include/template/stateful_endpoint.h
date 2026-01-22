#pragma once

#include <esp_http_server.h>
#include <ArduinoJson.h>
#include <template/stateful_service.h>
#include <communication/native_server.h>

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

    esp_err_t handleStateUpdate(httpd_req_t *request, JsonVariant &json) {
        JsonVariant jsonObject = json.as<JsonVariant>();
        StateUpdateResult outcome = _statefulService->updateWithoutPropagation(jsonObject, _stateUpdater);

        if (outcome == StateUpdateResult::ERROR) {
            return NativeServer::sendError(request, 400, "Invalid state");
        } else if ((outcome == StateUpdateResult::CHANGED)) {
            _statefulService->callUpdateHandlers(HTTP_ENDPOINT_ORIGIN_ID);
        }

        JsonDocument doc;
        JsonVariant root = doc.to<JsonVariant>();
        _statefulService->read(root, _stateReader);
        return NativeServer::sendJson(request, 200, doc);
    }

    esp_err_t getState(httpd_req_t *request) {
        JsonDocument doc;
        JsonVariant root = doc.to<JsonVariant>();
        _statefulService->read(root, _stateReader);
        return NativeServer::sendJson(request, 200, doc);
    }
};
