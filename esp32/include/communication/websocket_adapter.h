#pragma once

#include <esp_http_server.h>
#include <template/stateful_service.h>
#include <list>
#include <map>
#include <vector>
#include <string>

#include <communication/comm_base.hpp>

class Websocket : public CommAdapterBase {
  public:
    Websocket(httpd_handle_t* server_handle, const char* route = "/api/ws");

    void begin() override;

  private:
    httpd_handle_t* server_handle_;
    const char* route_;
    std::list<int> clients_;
    SemaphoreHandle_t client_mutex_;

    static esp_err_t wsHandler(httpd_req_t* req);
    void handleOpen(httpd_req_t* req);
    void handleClose(int fd);
    void handleFrame(httpd_req_t* req, httpd_ws_frame_t* frame);

    void send(const uint8_t* data, size_t len, int cid = -1) override;

    void addClient(int fd);
    void removeClientFromList(int fd);
};
