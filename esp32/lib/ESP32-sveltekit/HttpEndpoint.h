#ifndef HttpEndpoint_h
#define HttpEndpoint_h

#include <PsychicHttp.h>
#include <StatefulService.h>

#include <functional>

#define HTTP_ENDPOINT_ORIGIN_ID "http"
#define HTTPS_ENDPOINT_ORIGIN_ID "https"

template <class T>
class HttpEndpoint {
  protected:
    JsonStateReader<T> _stateReader;
    JsonStateUpdater<T> _stateUpdater;
    StatefulService<T> *_statefulService;
    size_t _bufferSize;
    PsychicHttpServer *_server;
    const char *_servicePath;

  public:
    HttpEndpoint(JsonStateReader<T> stateReader, JsonStateUpdater<T> stateUpdater, StatefulService<T> *statefulService,
                 PsychicHttpServer *server, const char *servicePath)
        : _stateReader(stateReader),
          _stateUpdater(stateUpdater),
          _statefulService(statefulService),
          _server(server),
          _servicePath(servicePath) {}

    // register the web server on() endpoints
    void begin() {
// OPTIONS (for CORS preflight)
#ifdef ENABLE_CORS
        _server->on(_servicePath, HTTP_OPTIONS, [this](PsychicRequest *request) { return request->reply(200); });
#endif

        // GET
        _server->on(_servicePath, HTTP_GET, [this](PsychicRequest *request) {
            PsychicJsonResponse response = PsychicJsonResponse(request, false);
            JsonObject jsonObject = response.getRoot();
            _statefulService->read(jsonObject, _stateReader);
            return response.send();
        });
        ESP_LOGV("HttpEndpoint", "Registered GET endpoint: %s", _servicePath);

        // POST
        _server->on(_servicePath, HTTP_POST, [this](PsychicRequest *request, JsonVariant &json) {
            if (!json.is<JsonObject>()) {
                return request->reply(400);
            }

            JsonObject jsonObject = json.as<JsonObject>();
            StateUpdateResult outcome = _statefulService->updateWithoutPropagation(jsonObject, _stateUpdater);

            if (outcome == StateUpdateResult::ERROR) {
                return request->reply(400);
            } else if ((outcome == StateUpdateResult::CHANGED)) {
                // persist the changes to the FS
                _statefulService->callUpdateHandlers(HTTP_ENDPOINT_ORIGIN_ID);
            }

            PsychicJsonResponse response = PsychicJsonResponse(request, false);
            jsonObject = response.getRoot();

            _statefulService->read(jsonObject, _stateReader);

            return response.send();
        });

        ESP_LOGV("HttpEndpoint", "Registered POST endpoint: %s", _servicePath);
    }
};

#endif
