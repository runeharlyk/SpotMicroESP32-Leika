#pragma once

#include <esp_http_server.h>
#include <map>
#include <list>
#include <functional>

namespace websocket {

struct WebSocketClient {
    int fd;
    uint64_t last_seen;
};

typedef std::function<void(int fd)> ClientCallback;
typedef std::function<esp_err_t(httpd_req_t *req, httpd_ws_frame_t *frame)> FrameCallback;

class WebSocketServer {
  public:
    WebSocketServer();
    ~WebSocketServer();

    void setOpenCallback(ClientCallback callback);
    void setCloseCallback(ClientCallback callback);
    void setFrameCallback(FrameCallback callback);

    esp_err_t handleWebSocket(httpd_req_t *req);

    void addClient(int fd);
    void removeClient(int fd);
    WebSocketClient *getClient(int fd);

    esp_err_t sendText(int fd, const char *data, size_t len);
    esp_err_t sendBinary(int fd, const uint8_t *data, size_t len);
    esp_err_t sendToAll(httpd_ws_type_t type, const uint8_t *data, size_t len);

    bool hasClients() const { return !_clients.empty(); }
    size_t clientCount() const { return _clients.size(); }

  public:
    void setServer(httpd_handle_t server) { _server = server; }

  private:
    std::map<int, WebSocketClient> _clients;
    ClientCallback _onOpen;
    ClientCallback _onClose;
    FrameCallback _onFrame;
    SemaphoreHandle_t _mutex;
    httpd_handle_t _server;

    friend esp_err_t websocket_handler(httpd_req_t *req);
};

esp_err_t websocket_handler(httpd_req_t *req);

} // namespace websocket
