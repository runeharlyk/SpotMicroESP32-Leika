#include <communication/websocket_adapter.h>

static const char* TAG = "Websocket";

Websocket::Websocket(PsychicHttpServer& server, const char* route) : server_(server), route_(route) {
    socket_.onOpen(std::bind(&Websocket::onWSOpen, this, std::placeholders::_1));
    socket_.onClose(std::bind(&Websocket::onWSClose, this, std::placeholders::_1));
    socket_.onFrame(std::bind(&Websocket::onFrame, this, std::placeholders::_1, std::placeholders::_2));
}

void Websocket::begin() { server_.on(route_, &socket_); }

void Websocket::onWSOpen(PsychicWebSocketClient* client) {
    ESP_LOGI(TAG, "Client connected: %s [%u]", client->remoteIP().toString().c_str(), client->socket());
    sendPong(client->socket());
}

void Websocket::onWSClose(PsychicWebSocketClient* client) {
    ESP_LOGI(TAG, "Client disconnected: %s [%u]", client->remoteIP().toString().c_str(), client->socket());
    removeClient(client->socket());
}

esp_err_t Websocket::onFrame(PsychicWebSocketRequest* request, httpd_ws_frame* frame) {
    if (frame->type != HTTPD_WS_TYPE_BINARY) {
        ESP_LOGW(TAG, "Expected binary frame, got type %d", frame->type);
        return ESP_OK;
    }

    handleIncoming(frame->payload, frame->len, request->client()->socket());
    return ESP_OK;
}

void Websocket::send(const uint8_t* data, size_t len, int cid) {
    if (cid >= 0) {
        auto* client = socket_.getClient(cid);
        if (client) {
            esp_err_t err = client->sendMessage(HTTPD_WS_TYPE_BINARY, data, len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send message to client %d: %s (len=%u)", cid, esp_err_to_name(err), len);
            }
        } else {
            ESP_LOGW(TAG, "Client %d not found for send", cid);
        }
    } else {
        socket_.sendAll(HTTPD_WS_TYPE_BINARY, data, len);
    }
}
