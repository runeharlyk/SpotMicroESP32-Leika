#ifndef Socket_h
#define Socket_h

#include <PsychicHttp.h>
#include <ArduinoJson.h>
#include <map>
#include <list>
#include <functional>

#include "event_bus.hpp"
#include "adapters/comm_base.hpp"
#include "topic.hpp"

class EventSocket : public CommBase<> {
    PsychicWebSocketHandler _socket;

  public:
    EventSocket();
    PsychicWebSocketHandler *getHandler() { return &_socket; }

  private:
    void send(size_t clientId, const char *data, size_t len) override;

    void handleReceive(const std::string &data);

    void onWSOpen(PsychicWebSocketClient *client);
    void onWSClose(PsychicWebSocketClient *client);
    esp_err_t onFrame(PsychicWebSocketRequest *request, httpd_ws_frame *frame);
};

extern EventSocket socket;

#endif
