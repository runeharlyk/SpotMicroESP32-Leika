#pragma once

#include <communication/http_server.h>
#include <template/stateful_service.h>
#include <pb_encode.h>
#include <pb_decode.h>

#include <functional>

#define HTTP_ENDPOINT_ORIGIN_ID "http"

template <class T, class ProtoT>
class StatefulHttpEndpoint {
  protected:
    ProtoStateReader<T, ProtoT> _stateReader;
    ProtoStateUpdater<T, ProtoT> _stateUpdater;
    StatefulService<T>* _statefulService;
    const pb_msgdesc_t* _fields;

  public:
    StatefulHttpEndpoint(ProtoStateReader<T, ProtoT> stateReader, ProtoStateUpdater<T, ProtoT> stateUpdater,
                         StatefulService<T>* statefulService, const pb_msgdesc_t* fields)
        : _stateReader(stateReader), _stateUpdater(stateUpdater), _statefulService(statefulService), _fields(fields) {}

    esp_err_t handleStateUpdate(HttpRequest& request) {
        ProtoT proto = {};
        if (!request.decodeProto(proto, _fields)) {
            return request.reply(400);
        }

        StateUpdateResult outcome = _statefulService->updateWithoutPropagation(proto, _stateUpdater);

        if (outcome == StateUpdateResult::ERROR)
            return request.reply(400);
        else if (outcome == StateUpdateResult::CHANGED) {
            _statefulService->callUpdateHandlers(HTTP_ENDPOINT_ORIGIN_ID);
        }

        ProtoT response = {};
        _statefulService->read(response, _stateReader);
        return request.replyProto(response, _fields);
    }

    esp_err_t getState(HttpRequest& request) {
        ProtoT proto = {};
        _statefulService->read(proto, _stateReader);
        return request.replyProto(proto, _fields);
    }
};
