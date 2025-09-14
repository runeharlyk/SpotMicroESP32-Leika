#ifndef Socket_h
#define Socket_h

#include <PsychicHttp.h>
#include <template/stateful_service.h>
#include <list>
#include <map>
#include <vector>
#include <string>

enum message_type_t { CONNECT = 0, DISCONNECT = 1, EVENT = 2, PING = 3, PONG = 4, BINARY_EVENT = 5 };

typedef std::function<void(JsonVariant &root, int originId)> EventCallback;
typedef std::function<void(const std::string &originId, bool sync)> SubscribeCallback;

class EventSocket {
  public:
    EventSocket(PsychicHttpServer &server, const char *route = "/api/ws");

    void begin();

    bool hasSubscribers(const char *event);

    void onEvent(std::string event, EventCallback callback);

    void onSubscribe(std::string event, SubscribeCallback callback);

    void emit(const char *event, JsonVariant &payload, const char *originId = "", bool onlyToSameOrigin = false);

  private:
    PsychicWebSocketHandler _socket;
    PsychicHttpServer &_server;
    const char *_route;

    std::map<std::string, std::list<int>> client_subscriptions;
    std::map<std::string, std::list<EventCallback>> event_callbacks;
    std::map<std::string, std::list<SubscribeCallback>> subscribe_callbacks;
    void handleEventCallbacks(std::string event, JsonVariant &jsonObject, int originId);
    void send(PsychicWebSocketClient *client, const char *data, size_t len);
    void handleSubscribeCallbacks(std::string event, const std::string &originId);

    void onWSOpen(PsychicWebSocketClient *client);
    void onWSClose(PsychicWebSocketClient *client);
    esp_err_t onFrame(PsychicWebSocketRequest *request, httpd_ws_frame *frame);
};

#endif
