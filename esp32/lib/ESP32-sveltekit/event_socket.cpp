#include <event_socket.h>

SemaphoreHandle_t clientSubscriptionsMutex = xSemaphoreCreateMutex();

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

    JsonDocument doc;

#if USE_MSGPACK
    if (frame->type != HTTPD_WS_TYPE_BINARY) {
        ESP_LOGE("EventSocket", "Unsupported frame type: %d", frame->type);
        return ESP_OK;
    }
    if (deserializeMsgPack(doc, frame->payload, frame->len)) {
        ESP_LOGE("EventSocket", "Could not deserialize msgpack");
        return ESP_OK;
    };
#else
    if (frame->type != HTTPD_WS_TYPE_TEXT) {
        ESP_LOGE("EventSocket", "Unsupported frame type: %d", frame->type);
        return ESP_OK;
    }
    if (deserializeJson(doc, frame->payload, frame->len)) {
        ESP_LOGE("EventSocket", "Could not deserialize json");
        return ESP_OK;
    };
#endif

    serializeJson(doc, Serial);
    Serial.println();

    auto msg = doc.as<JsonArray>();

    message_type_t message_type = static_cast<message_type_t>(msg[0].as<uint8_t>());

    if (message_type == PONG) {
        ESP_LOGV("EventSocket", "Pong");
        return ESP_OK;
    } else if (message_type == PING) {
        ESP_LOGV("EventSocket", "Ping");
#if USE_MSGPACK
        const uint8_t out[] = {0x91, 0x04};
        send(request->client(), reinterpret_cast<const char *>(out), sizeof(out));
#else
        const char *out = "[4]";
        send(request->client(), out, strlen(out));
#endif
        return ESP_OK;
    }

    const char *event = msg[1].as<const char *>();

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
        JsonObject jsonObject = msg[2].as<JsonObject>();
        handleEventCallbacks(event, jsonObject, request->client()->socket());
        return ESP_OK;
    }
    return ESP_OK;
}

bool EventSocket::hasSubscribers(const char *event) { return !client_subscriptions[event].empty(); }

void EventSocket::emit(const char *event, JsonVariant &payload, const char *originId, bool onlyToSameOrigin) {
    int originSubscriptionId = originId[0] ? atoi(originId) : -1;
    xSemaphoreTake(clientSubscriptionsMutex, portMAX_DELAY);
    auto &subscriptions = client_subscriptions[event];
    if (subscriptions.empty()) {
        xSemaphoreGive(clientSubscriptionsMutex);
        return;
    }

    JsonDocument doc;
    auto a = doc.to<JsonArray>();
    a.add(static_cast<uint8_t>(message_type_t::EVENT));
    a.add(event);
    a.add(payload);

    String out;
#if USE_MSGPACK
    serializeMsgPack(doc, out);
#else
    serializeJson(doc, out);
#endif

    const char *msg = out.c_str();

    // if onlyToSameOrigin == true, send the message back to the origin
    if (onlyToSameOrigin && originSubscriptionId > 0) {
        auto *client = _socket.getClient(originSubscriptionId);
        if (client) {
            ESP_LOGV("EventSocket", "Emitting event: %s to %s, Message: %s", event,
                     client->remoteIP().toString().c_str(), msg);
            send(client, msg, strlen(msg));
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
            send(client, msg, strlen(msg));
        }
    }
    xSemaphoreGive(clientSubscriptionsMutex);
}

void EventSocket::send(PsychicWebSocketClient *client, const char *data, size_t len) {
    if (!client) return;

#if USE_MSGPACK
    client->sendMessage(HTTPD_WS_TYPE_BINARY, data, len);
#else
    client->sendMessage(HTTPD_WS_TYPE_TEXT, data, len);
#endif
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