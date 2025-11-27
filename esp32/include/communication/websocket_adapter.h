#ifndef Socket_h
#define Socket_h

#include <esp_http_server.h>
#include <template/stateful_service.h>
#include <utils/websocket_server.h>
#include <list>
#include <map>
#include <vector>
#include <string>

#include <communication/comm_base.hpp>

class Websocket : CommAdapterBase {
  public:
    Websocket(httpd_handle_t *server, const char *route = "/api/ws");

    void begin() override;

    void onEvent(std::string event, EventCallback callback);

    void emit(const char *event, JsonVariant &payload, const char *originId = "", bool onlyToSameOrigin = false);

    httpd_uri_t *getUriHandler() { return &_ws_uri; }

  private:
    websocket::WebSocketServer _socket;
    httpd_handle_t *_server;
    const char *_route;
    httpd_uri_t _ws_uri;

    void onWSOpen(int fd);
    void onWSClose(int fd);
    esp_err_t onFrame(httpd_req_t *req, httpd_ws_frame_t *frame);

    void send(const uint8_t *data, size_t len, int cid = -1) override;

    static esp_err_t ws_handler_wrapper(httpd_req_t *req);
};

#endif
