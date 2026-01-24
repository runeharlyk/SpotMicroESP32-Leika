#pragma once

#include <esp_http_server.h>
#include <template/stateful_service.h>
#include <communication/native_server.h>
#include <platform_shared/api.pb.h>
#include <pb_encode.h>
#include <pb_decode.h>

#include <functional>

#define PROTO_ENDPOINT_ORIGIN_ID "proto"

/**
 * A stateful HTTP endpoint that uses protobuf encoding with api::Request/Response wrappers.
 *
 * @tparam T The internal state type (e.g., APSettings C++ class)
 * @tparam ProtoT The protobuf message type within the oneof (e.g., api_APSettings)
 *
 * The endpoint receives api::Request, extracts the specific payload from the oneof,
 * and returns api::Response with the updated state.
 */
template <class T, class ProtoT>
class StatefulProtoEndpoint {
  public:
    /** Converts internal state to protobuf message for responses */
    // Formats are passed as referenced const (local variable) we want to read from, and a reference (proto) we write to
    using ProtoStateReader = std::function<void(const T&, ProtoT&)>;
    /** Converts incoming protobuf message to internal state */
    // Formats are passed as referenced const (new object) we read from, and a reference to the local variable we  write to
    using ProtoStateUpdater = std::function<StateUpdateResult(const ProtoT&, T&)>;
    /** Extracts the specific proto type from Request oneof */
    using RequestExtractor = std::function<bool(const api_Request&, ProtoT&)>;
    /** Assigns the specific proto type to Response oneof */
    using ResponseAssigner = std::function<void(api_Response&, const ProtoT&)>;

  protected:
    ProtoStateReader _stateReader;
    ProtoStateUpdater _stateUpdater;
    StatefulService<T>* _statefulService;
    RequestExtractor _requestExtractor;
    ResponseAssigner _responseAssigner;

  public:
    /**
     * Constructor for wrapped proto endpoint
     * @param stateReader Converts internal state to proto
     * @param stateUpdater Converts proto to internal state
     * @param statefulService The stateful service to manage
     * @param requestExtractor Extracts specific type from Request oneof
     * @param responseAssigner Assigns specific type to Response oneof
     */
    StatefulProtoEndpoint(ProtoStateReader stateReader, ProtoStateUpdater stateUpdater,
                          StatefulService<T>* statefulService,
                          RequestExtractor requestExtractor, ResponseAssigner responseAssigner)
        : _stateReader(stateReader),
          _stateUpdater(stateUpdater),
          _statefulService(statefulService),
          _requestExtractor(requestExtractor),
          _responseAssigner(responseAssigner) {}

    /**
     * Handles POST requests: extracts payload from pre-decoded Request, updates state, returns Response
     */
    esp_err_t handleStateUpdate(httpd_req_t* httpReq, api_Request* protoReq) {
        ProtoT protoMsg = {};
        if (!_requestExtractor(*protoReq, protoMsg)) {
            return sendErrorResponse(httpReq, 400, "Invalid request type");
        }

        StateUpdateResult outcome = _statefulService->update(
            [this, &protoMsg](T& settings) { return _stateUpdater(protoMsg, settings); }, PROTO_ENDPOINT_ORIGIN_ID);

        if (outcome == StateUpdateResult::ERROR) {
            return sendErrorResponse(httpReq, 400, "Invalid state");
        }

        return sendStateResponse(httpReq, 200);
    }

    /**
     * Handles GET requests: reads current state and returns it as Response
     */
    esp_err_t getState(httpd_req_t* request) {
        return sendStateResponse(request, 200);
    }

  private:
    /** Sends current state wrapped in Response */
    esp_err_t sendStateResponse(httpd_req_t* request, uint32_t statusCode) {
        api_Response res = api_Response_init_zero;
        res.status_code = statusCode;

        ProtoT protoState = {};
        _statefulService->read([this, &protoState](const T& settings) { _stateReader(settings, protoState); });
        _responseAssigner(res, protoState);

        return NativeServer::sendProto(request, 200, res, api_Response_fields);
    }

    /** Sends error wrapped in Response */
    esp_err_t sendErrorResponse(httpd_req_t* request, uint32_t statusCode, const char* message) {
        api_Response res = api_Response_init_zero;
        res.status_code = statusCode;
        res.error_message = (char*)message;
        return NativeServer::sendProto(request, statusCode == 200 ? 200 : 400, res, api_Response_fields);
    }
};

// =============================================================================
// Helper macros for defining request extractors and response assigners
// =============================================================================

/**
 * Creates a request extractor lambda for a specific payload type
 * Usage: API_REQUEST_EXTRACTOR(ap_settings, api_APSettings)
 */
#define API_REQUEST_EXTRACTOR(field_name, proto_type)                              \
    [](const api_Request& req, proto_type& out) -> bool {                          \
        if (req.which_payload == api_Request_##field_name##_tag) {                 \
            out = req.payload.field_name;                                          \
            return true;                                                           \
        }                                                                          \
        return false;                                                              \
    }

/**
 * Creates a response assigner lambda for a specific payload type
 * Usage: API_RESPONSE_ASSIGNER(ap_settings, api_APSettings)
 */
#define API_RESPONSE_ASSIGNER(field_name, proto_type)                              \
    [](api_Response& res, const proto_type& data) {                                \
        res.which_payload = api_Response_##field_name##_tag;                       \
        res.payload.field_name = data;                                             \
    }
