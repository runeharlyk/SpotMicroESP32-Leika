#ifndef Socket_h
#define Socket_h

#include <PsychicHttp.h>
#include <template/stateful_service.h>
#include <list>
#include <map>
#include <vector>

enum message_type_t { CONNECT = 0, DISCONNECT = 1, EVENT = 2, PING = 3, PONG = 4, BINARY_EVENT = 5 };

typedef std::function<void(JsonObject &root, int originId)> EventCallback;
typedef std::function<void(const String &originId, bool sync)> SubscribeCallback;

class EventSocket {
  public:
    EventSocket();

    PsychicWebSocketHandler *getHandler() { return &_socket; }

    bool hasSubscribers(const char *event);

    void onEvent(String event, EventCallback callback);

    void onSubscribe(String event, SubscribeCallback callback);

    void emit(const char *event, const char *payload, const char *originId = "", bool onlyToSameOrigin = false);
    // if onlyToSameOrigin == true, the message will be sent to the originId only, otherwise it will be broadcasted to
    // all clients except the originId

  private:
    PsychicWebSocketHandler _socket;

    std::map<String, std::list<int>> client_subscriptions;
    std::map<String, std::list<EventCallback>> event_callbacks;
    std::map<String, std::list<SubscribeCallback>> subscribe_callbacks;
    void handleEventCallbacks(String event, JsonObject &jsonObject, int originId);
    void handleSubscribeCallbacks(String event, const String &originId);

    void onWSOpen(PsychicWebSocketClient *client);
    void onWSClose(PsychicWebSocketClient *client);
    esp_err_t onFrame(PsychicWebSocketRequest *request, httpd_ws_frame *frame);
};

extern EventSocket socket;

#endif
