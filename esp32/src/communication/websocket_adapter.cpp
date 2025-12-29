#include <communication/websocket_adapter.h>
#include <string>

static const char *TAG = "Websocket";

Websocket::Websocket(PsychicHttpServer &server, const char *route) : _server(server), _route(route) {
    _socket.onOpen((std::bind(&Websocket::onWSOpen, this, std::placeholders::_1)));
    _socket.onClose(std::bind(&Websocket::onWSClose, this, std::placeholders::_1));
    _socket.onFrame(std::bind(&Websocket::onFrame, this, std::placeholders::_1, std::placeholders::_2));
}

void Websocket::begin() { _server.on(_route, &_socket); }

void Websocket::onEvent(std::string event, EventCallback callback) {
    CommAdapterBase::onEvent(std::move(event), std::move(callback));
}

void Websocket::emit(const char *event, JsonVariant &payload, const char *originId, bool onlyToSameOrigin) {
    CommAdapterBase::emit(event, payload, originId, onlyToSameOrigin);
}

void Websocket::emit_raw(const char *event, uint8_t* payload, size_t event_length, size_t payload_length) {
    size_t total_len = payload_length + event_length + 1;
    uint8_t* buf = (uint8_t*) malloc(total_len + 1);
    memcpy(buf, event, event_length);
    buf[event_length+1] = ',';
    memcpy(buf + event_length+1, payload, payload_length);
    send(buf, total_len, -1);
    free(buf);
}

void Websocket::onWSOpen(PsychicWebSocketClient *client) {
    ESP_LOGI("EventSocket", "ws[%s][%u] connect", client->remoteIP().toString().c_str(), client->socket());
    ping(client->socket());
}

void Websocket::onWSClose(PsychicWebSocketClient *client) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (auto &event_subscriptions : client_subscriptions) {
        event_subscriptions.second.remove(client->socket());
    }
    xSemaphoreGive(mutex_);
    ESP_LOGI("EventSocket", "ws[%s][%u] disconnect", client->remoteIP().toString().c_str(), client->socket());
}

esp_err_t Websocket::onFrame(PsychicWebSocketRequest *request, httpd_ws_frame *frame) {
    ESP_LOGV(TAG, "ws[%s][%u] opcode[%d]", request->client()->remoteIP().toString().c_str(),
             request->client()->socket(), frame->type);

    if (frame->type != HTTPD_WS_TYPE_TEXT && frame->type != HTTPD_WS_TYPE_BINARY) {
        ESP_LOGE(TAG, "Unsupported frame type: %d", frame->type);
        return ESP_OK;
    }

#if USE_MSGPACK
    if (frame->type == HTTPD_WS_TYPE_BINARY) {
        handleIncoming(frame->payload, frame->len, request->client()->socket());
    } else {
        ESP_LOGE(TAG, "Expected binary, got text");
    }
#else
    if (frame->type == HTTPD_WS_TYPE_TEXT) {
        handleIncoming(frame->payload, frame->len, request->client()->socket());
    } else {
        ESP_LOGE(TAG, "Expected text, got binary");
    }
#endif

    return ESP_OK;
}

void Websocket::send(const uint8_t *data, size_t len, int cid) {
    if (cid != -1) {
        auto *client = _socket.getClient(cid);
        if (client) {
            ESP_LOGV(TAG, "Sending to client %s: %s", client->remoteIP().toString().c_str(), data);
#if USE_MSGPACK
            client->sendMessage(HTTPD_WS_TYPE_BINARY, data, len);
#else
            client->sendMessage(HTTPD_WS_TYPE_TEXT, data, len);
#endif
        }
    } else {
        ESP_LOGV(TAG, "Sending to all clients: %s", data);
#if USE_MSGPACK
        _socket.sendAll(HTTPD_WS_TYPE_BINARY, data, len);
#else
        _socket.sendAll(HTTPD_WS_TYPE_TEXT, data, len);
#endif
    }
}
