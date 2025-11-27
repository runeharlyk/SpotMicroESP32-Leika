#include <communication/websocket_adapter.h>
#include <string>

static const char *TAG = "Websocket";

Websocket::Websocket(httpd_handle_t *server, const char *route) : _server(server), _route(route) {
    _socket.setOpenCallback([this](int fd) { this->onWSOpen(fd); });
    _socket.setCloseCallback([this](int fd) { this->onWSClose(fd); });
    _socket.setFrameCallback([this](httpd_req_t *req, httpd_ws_frame_t *frame) { return this->onFrame(req, frame); });

    _ws_uri.uri = _route;
    _ws_uri.method = HTTP_GET;
    _ws_uri.handler = ws_handler_wrapper;
    _ws_uri.user_ctx = this;
    _ws_uri.is_websocket = true;
    _ws_uri.handle_ws_control_frames = false;
}

void Websocket::begin() {
    if (_server && *_server) {
        _socket.setServer(*_server);
        httpd_register_uri_handler(*_server, &_ws_uri);
    }
}

void Websocket::onEvent(std::string event, EventCallback callback) {
    CommAdapterBase::onEvent(std::move(event), std::move(callback));
}

void Websocket::emit(const char *event, JsonVariant &payload, const char *originId, bool onlyToSameOrigin) {
    CommAdapterBase::emit(event, payload, originId, onlyToSameOrigin);
}

void Websocket::onWSOpen(int fd) {
    ESP_LOGI(TAG, "ws[%d] connect", fd);
    ping(fd);
}

void Websocket::onWSClose(int fd) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (auto &event_subscriptions : client_subscriptions) {
        event_subscriptions.second.remove(fd);
    }
    xSemaphoreGive(mutex_);
    ESP_LOGI(TAG, "ws[%d] disconnect", fd);
}

esp_err_t Websocket::onFrame(httpd_req_t *req, httpd_ws_frame_t *frame) {
    int fd = httpd_req_to_sockfd(req);
    ESP_LOGV(TAG, "ws[%d] opcode[%d]", fd, frame->type);

    if (frame->type != HTTPD_WS_TYPE_TEXT && frame->type != HTTPD_WS_TYPE_BINARY) {
        ESP_LOGE(TAG, "Unsupported frame type: %d", frame->type);
        return ESP_OK;
    }

#if USE_PROTOBUF
    if (frame->type == HTTPD_WS_TYPE_BINARY) {
        handleIncoming(frame->payload, frame->len, fd);
    } else {
        ESP_LOGE(TAG, "Expected binary, got text");
    }
#elif USE_MSGPACK
    if (frame->type == HTTPD_WS_TYPE_BINARY) {
        handleIncoming(frame->payload, frame->len, fd);
    } else {
        ESP_LOGE(TAG, "Expected binary, got text");
    }
#else
    if (frame->type == HTTPD_WS_TYPE_TEXT) {
        handleIncoming(frame->payload, frame->len, fd);
    } else {
        ESP_LOGE(TAG, "Expected text, got binary");
    }
#endif

    return ESP_OK;
}

void Websocket::send(const uint8_t *data, size_t len, int cid) {
    if (!_server || !*_server) {
        ESP_LOGW(TAG, "Server not initialized, cannot send");
        return;
    }

    httpd_ws_type_t type;
#if USE_PROTOBUF || USE_MSGPACK
    type = HTTPD_WS_TYPE_BINARY;
#else
    type = HTTPD_WS_TYPE_TEXT;
#endif

    if (cid != -1) {
        ESP_LOGV(TAG, "Sending to client %d: %.*s", cid, (int)len, data);

        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.payload = (uint8_t *)data;
        ws_pkt.len = len;
        ws_pkt.type = type;

        httpd_ws_send_frame_async(*_server, cid, &ws_pkt);
    } else {
        ESP_LOGV(TAG, "Sending to all clients: %.*s", (int)len, data);
        _socket.sendToAll(type, data, len);
    }
}

esp_err_t Websocket::ws_handler_wrapper(httpd_req_t *req) {
    Websocket *socket = (Websocket *)req->user_ctx;
    return socket->_socket.handleWebSocket(req);
}
