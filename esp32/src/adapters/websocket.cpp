#include <adapters/websocket.hpp>

EventSocket::EventSocket() {
    _socket.onOpen((std::bind(&EventSocket::onWSOpen, this, std::placeholders::_1)));
    _socket.onClose(std::bind(&EventSocket::onWSClose, this, std::placeholders::_1));
    _socket.onFrame(std::bind(&EventSocket::onFrame, this, std::placeholders::_1, std::placeholders::_2));

#define X(e, t)                                                                  \
    setHandle<t>(Topic::e, EventBus<t>::subscribe([this](const t* d, size_t n) { \
                     if (n) this->emit<Topic::e>(d[0]);                          \
                 }));
    TOPIC_LIST
#undef X
}

void EventSocket::onWSOpen(PsychicWebSocketClient* client) {
    ESP_LOGI("EventSocket", "ws[%s][%u] connect", client->remoteIP().toString().c_str(), client->socket());
}

void EventSocket::onWSClose(PsychicWebSocketClient* client) {
    for (size_t i = 0; i < static_cast<size_t>(Topic::COUNT); ++i) {
        unsubscribe(static_cast<Topic>(i), client->socket());
    }

    ESP_LOGI("EventSocket", "ws[%s][%u] disconnect", client->remoteIP().toString().c_str(), client->socket());
}

esp_err_t EventSocket::onFrame(PsychicWebSocketRequest* request, httpd_ws_frame* frame) {
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

    auto payload = doc.as<JsonArrayConst>();

    MsgKind msgKind = static_cast<MsgKind>(payload[0].as<uint8_t>());
    switch (msgKind) {
        case MsgKind::Connect:
            for (size_t i = 1; i < payload.size(); ++i) {
                subscribe(static_cast<Topic>(payload[i].as<uint8_t>()), request->client()->socket());
            }
            break;

        case MsgKind::Disconnect:
            for (size_t i = 1; i < payload.size(); ++i) {
                Topic t = static_cast<Topic>(payload[i].as<uint8_t>());
                unsubscribe(t, request->client()->socket());
            }
            break;

        case MsgKind::Event:
            if (payload.size() < 3) break;
            switch (static_cast<Topic>(payload[1].as<uint8_t>())) {
#define X(e, m) \
    case Topic::e: EventBus<m>::publishAsync(parse<m>(payload[2]), getHandle<m>(Topic::e)); break;
                TOPIC_LIST
#undef X
                default: break;
            }
            break;

        case MsgKind::Ping: ESP_LOGV("EventSocket", "Ping"); break;

        case MsgKind::Pong: ESP_LOGV("EventSocket", "Pong"); break;

        default: break;
    }
    return ESP_OK;
}

void EventSocket::send(size_t clientId, const char* data, size_t len) {
    auto* client = _socket.getClient(clientId);
    if (!client) return;

#if USE_MSGPACK
    client->sendMessage(HTTPD_WS_TYPE_BINARY, data, len);
#else
    client->sendMessage(HTTPD_WS_TYPE_TEXT, data, len);
#endif
}

EventSocket socket;