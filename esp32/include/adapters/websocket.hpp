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
// #include "msgs/motion_input_msg.hpp"
// #include "msgs/motion_angles_msg.hpp"
// #include "msgs/motion_position_msg.hpp"
// #include "msgs/motion_mode_msg.hpp"

// typedef std::function<void(JsonObject &root, int originId)> EventCallback;

class EventSocket : public CommBase<> {
    PsychicWebSocketHandler _socket;

  public:
    EventSocket();
    PsychicWebSocketHandler *getHandler() { return &_socket; }

  private:
    void send(size_t clientId, const char *data, size_t len) override;

    void handleReceive(const std::string &data);
    // void handleTypedMessage(const std::string &data);
    // void handleLegacyMessage(const std::string &data, int originId);
    // void handleEventCallbacks(String event, JsonObject &jsonObject, int originId);

    void onWSOpen(PsychicWebSocketClient *client);
    void onWSClose(PsychicWebSocketClient *client);
    esp_err_t onFrame(PsychicWebSocketRequest *request, httpd_ws_frame *frame);
};

extern EventSocket socket;

#endif
