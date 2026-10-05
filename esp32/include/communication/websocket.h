#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <communication/webserver.h>
#include <communication/comm_base.hpp>
#include <communication/stall_tracker.h>

class Websocket : public CommAdapterBase {
  public:
    Websocket(WebServer& server, const char* route = "/api/ws");

    void begin() override;

    /** Called on the socket's task when a client's session ends, however it ends. */
    void onClose(std::function<void(int cid)> listener) { closeListener_ = std::move(listener); }

    /** The connection a client id stands for now; the id of a closed socket is reused by the next one. */
    uint32_t session(int cid);

    /** Emits only while `cid` is still the connection `session` was taken from, for replies sent later. */
    template <typename T>
    void emitToSession(const T& data, int cid, uint32_t session) {
        if (isSession(cid, session)) emit(data, cid);
    }

  private:
    WebServer& server_;
    const char* route_;
    SemaphoreHandle_t sessionsMutex_;
    std::map<int, uint32_t> sessions_;
    uint32_t nextSession_ = 1;
    std::function<void(int)> closeListener_;
    // A client that has taken no frame for this long is closed, so the frames its socket holds are freed.
    static constexpr uint32_t STALL_LIMIT_MS = 10000;
    StallTracker stalls_ {STALL_LIMIT_MS};

    bool isSession(int cid, uint32_t session);

    void onWsOpen(httpd_req_t* req);
    void onWsClose(int sockfd);
    esp_err_t onFrame(httpd_req_t* req, httpd_ws_frame_t* frame);

    bool send(const uint8_t* data, size_t len, int cid) override;
    bool ready(int cid) override;
};
