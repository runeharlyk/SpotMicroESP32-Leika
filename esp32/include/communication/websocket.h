#pragma once

#include <Arduino.h>
#include <communication/webserver.h>
#include <communication/comm_base.hpp>

class Websocket : public CommAdapterBase {
  public:
    Websocket(WebServer& server, const char* route = "/api/ws");

    void begin() override;

  private:
    WebServer& server_;
    const char* route_;

    void onWsOpen(httpd_req_t* req);
    void onWsClose(int sockfd);
    esp_err_t onFrame(httpd_req_t* req, httpd_ws_frame_t* frame);

    void send(const uint8_t* data, size_t len, int cid = -1) override;
};
