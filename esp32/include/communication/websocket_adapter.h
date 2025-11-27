#ifndef Socket_h
#define Socket_h

#include <PsychicHttp.h>
#include <template/stateful_service.h>
#include <list>
#include <map>
#include <vector>
#include <string>

#include <communication/comm_base.hpp>

class Websocket : public CommAdapterBase {
  public:
    Websocket(PsychicHttpServer &server, const char *route = "/api/ws");

    void begin() override;

    void onEvent(std::string event, EventCallback callback);

    void emit(const char *event, JsonVariant &payload, const char *originId = "", bool onlyToSameOrigin = false);

  private:
    PsychicWebSocketHandler _socket;
    PsychicHttpServer &_server;
    const char *_route;

    void onWSOpen(PsychicWebSocketClient *client);
    void onWSClose(PsychicWebSocketClient *client);
    esp_err_t onFrame(PsychicWebSocketRequest *request, httpd_ws_frame *frame);

    void send(const uint8_t *data, size_t len, int cid = -1) override;
};

#endif

