#include <event_socket.h>

SemaphoreHandle_t clientSubscriptionsMutex = xSemaphoreCreateMutex();

message_type_t char_to_message_type(char c) {
    switch (c) {
        case '0': return CONNECT;
        case '1': return DISCONNECT;
        case '2': return EVENT;
        case '3': return PING;
        case '4': return PONG;
        case '5': return BINARY_EVENT;
        default: throw std::invalid_argument("Invalid message type");
    }
}

const char *getEventName(const char *msg) {
    const char *start = strchr(msg, '/');
    if (!start) return nullptr;
    start++;
    const char *end = strchr(start, '[');
    if (!end) return start;

    static char eventName[32];
    int len = end - start;
    strncpy(eventName, start, len);
    eventName[len] = '\0';
    return eventName;
}

const char *getEventPayload(const char *msg) {
    const char *start = strchr(msg + 2, '[');
    const char *end = msg + strlen(msg) - 1;
    if (*start == '[') {
        start++;
    }
    int len = end - start;
    if (len < 0) return nullptr;
    char *payload = new char[len + 1];
    strncpy(payload, start, len);
    payload[len] = '\0';
    return payload;
}

EventSocket::EventSocket() {
    _socket.onOpen((std::bind(&EventSocket::onWSOpen, this, std::placeholders::_1)));
    _socket.onClose(std::bind(&EventSocket::onWSClose, this, std::placeholders::_1));
    _socket.onFrame(std::bind(&EventSocket::onFrame, this, std::placeholders::_1, std::placeholders::_2));
}

void EventSocket::onWSOpen(PsychicWebSocketClient *client) {
    ESP_LOGI("EventSocket", "ws[%s][%u] connect", client->remoteIP().toString().c_str(), client->socket());
}

void EventSocket::onWSClose(PsychicWebSocketClient *client) {
    xSemaphoreTake(clientSubscriptionsMutex, portMAX_DELAY);
    for (auto &event_subscriptions : client_subscriptions) {
        event_subscriptions.second.remove(client->socket());
    }
    xSemaphoreGive(clientSubscriptionsMutex);
    ESP_LOGI("EventSocket", "ws[%s][%u] disconnect", client->remoteIP().toString().c_str(), client->socket());
}

esp_err_t EventSocket::onFrame(PsychicWebSocketRequest *request, httpd_ws_frame *frame) {
    ESP_LOGV("EventSocket", "ws[%s][%u] opcode[%d]", request->client()->remoteIP().toString().c_str(),
             request->client()->socket(), frame->type);

    if (frame->type != HTTPD_WS_TYPE_TEXT) {
        ESP_LOGE("EventSocket", "Unsupported frame type");
        return ESP_OK;
    }

    ESP_LOGV("EventSocket", "Received message: %s", (char *)frame->payload);
    char *msg = (char *)frame->payload;

    message_type_t message_type = char_to_message_type(msg[0]);

    if (message_type == PING) {
        ESP_LOGV("EventSocket", "Ping");
        request->client()->sendMessage("3");
        return ESP_OK;
    } else if (message_type == PONG) {
        ESP_LOGV("EventSocket", "Pong");
        return ESP_OK;
    }

    const char *event = getEventName(msg);

    if (!event) {
        ESP_LOGE("EventSocket", "Invalid event name");
        return ESP_OK;
    }

    if (message_type == CONNECT) {
        ESP_LOGV("EventSocket", "Connect: %s", event);
        client_subscriptions[event].push_back(request->client()->socket());
        handleSubscribeCallbacks(event, String(request->client()->socket()));
    } else if (message_type == DISCONNECT) {
        ESP_LOGV("EventSocket", "Disconnect: %s", event);
        client_subscriptions[event].remove(request->client()->socket());
    } else if (message_type == EVENT) {
        const char *payload = getEventPayload(msg);
        if (!payload) {
            ESP_LOGE("EventSocket", "Invalid event payload");
            return ESP_OK;
        }
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            ESP_LOGE("EventSocket", "Failed to parse JSON payload");
            return ESP_OK;
        }
        JsonObject jsonObject = doc.as<JsonObject>();
        handleEventCallbacks(event, jsonObject, request->client()->socket());
        return ESP_OK;
    }
    return ESP_OK;
}

bool EventSocket::hasSubscribers(const char *event) { return !client_subscriptions[event].empty(); }

void EventSocket::emit(const char *event, const char *payload, const char *originId, bool onlyToSameOrigin) {
    int originSubscriptionId = originId[0] ? atoi(originId) : -1;
    xSemaphoreTake(clientSubscriptionsMutex, portMAX_DELAY);
    auto &subscriptions = client_subscriptions[event];
    if (subscriptions.empty()) {
        xSemaphoreGive(clientSubscriptionsMutex);
        return;
    }
    char msg[strlen(event) + strlen(payload) + 10];
    snprintf(msg, sizeof(msg), "2/%s[%s]", event, payload);

    // if onlyToSameOrigin == true, send the message back to the origin
    if (onlyToSameOrigin && originSubscriptionId > 0) {
        auto *client = _socket.getClient(originSubscriptionId);
        if (client) {
            ESP_LOGV("EventSocket", "Emitting event: %s to %s, Message: %s", event,
                     client->remoteIP().toString().c_str(), msg);
            client->sendMessage(msg);
        }
    } else { // else send the message to all other clients

        for (int subscription : client_subscriptions[event]) {
            if (subscription == originSubscriptionId) continue;
            auto *client = _socket.getClient(subscription);
            if (!client) {
                subscriptions.remove(subscription);
                continue;
            }
            ESP_LOGV("EventSocket", "Emitting event: %s to %s, Message: %s", event,
                     client->remoteIP().toString().c_str(), msg);
            client->sendMessage(msg);
        }
    }
    xSemaphoreGive(clientSubscriptionsMutex);
}

void EventSocket::handleEventCallbacks(String event, JsonObject &jsonObject, int originId) {
    for (auto &callback : event_callbacks[event]) {
        callback(jsonObject, originId);
    }
}

void EventSocket::handleSubscribeCallbacks(String event, const String &originId) {
    for (auto &callback : subscribe_callbacks[event]) {
        callback(originId, true);
    }
}

void EventSocket::onEvent(String event, EventCallback callback) {
    event_callbacks[event].push_back(std::move(callback));
}

void EventSocket::onSubscribe(String event, SubscribeCallback callback) {
    subscribe_callbacks[event].push_back(std::move(callback));
}

EventSocket socket;