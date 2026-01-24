#include <communication/websocket.h>
#include <esp_log.h>

static const char* TAG = "Websocket";

Websocket::Websocket(WebServer& server, const char* route) : server_(server), route_(route) {}

void Websocket::begin() {
    server_.onWsOpen([this](httpd_req_t* req) { onWsOpen(req); });
    server_.onWsClose([this](int sockfd) { onWsClose(sockfd); });
    server_.onWsFrame([this](httpd_req_t* req, httpd_ws_frame_t* frame) { return onFrame(req, frame); });
    server_.registerWebsocket(route_);
}

void Websocket::onWsOpen(httpd_req_t* req) {
    int sockfd = httpd_req_to_sockfd(req);
    ESP_LOGI(TAG, "Client connected: %d", sockfd);
    sendPong(sockfd);
}

void Websocket::onWsClose(int sockfd) {
    ESP_LOGI(TAG, "Client disconnected: %d", sockfd);
    removeClient(sockfd);
}

esp_err_t Websocket::onFrame(httpd_req_t* req, httpd_ws_frame_t* frame) {
    if (frame->type != HTTPD_WS_TYPE_BINARY) {
        ESP_LOGW(TAG, "Expected binary frame, got type %d", frame->type);
        return ESP_OK;
    }

    int sockfd = httpd_req_to_sockfd(req);
    handleIncoming(frame->payload, frame->len, sockfd);
    return ESP_OK;
}

void Websocket::send(const uint8_t* data, size_t len, int cid) {
    if (cid >= 0) {
        esp_err_t err = server_.wsSend(cid, data, len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send message to client %d: %s (len=%u)", cid, esp_err_to_name(err), len);
        }
    } else {
        server_.wsSendAll(data, len);
    }
}
