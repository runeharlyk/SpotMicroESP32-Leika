#include <utils/websocket_server.h>
#include <esp_log.h>
#include <string.h>

namespace websocket {

static const char *TAG = "WebSocketServer";

WebSocketServer::WebSocketServer() : _server(nullptr) { _mutex = xSemaphoreCreateMutex(); }

WebSocketServer::~WebSocketServer() {
    if (_mutex) {
        vSemaphoreDelete(_mutex);
    }
}

void WebSocketServer::setOpenCallback(ClientCallback callback) { _onOpen = callback; }

void WebSocketServer::setCloseCallback(ClientCallback callback) { _onClose = callback; }

void WebSocketServer::setFrameCallback(FrameCallback callback) { _onFrame = callback; }

esp_err_t WebSocketServer::handleWebSocket(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        addClient(fd);
        ESP_LOGI(TAG, "WebSocket client connected: fd=%d", fd);

        if (_onOpen) {
            _onOpen(fd);
        }

        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    if (ws_pkt.len) {
        ws_pkt.payload = (uint8_t *)malloc(ws_pkt.len + 1);
        if (!ws_pkt.payload) {
            ESP_LOGE(TAG, "Failed to allocate memory for WebSocket payload");
            return ESP_ERR_NO_MEM;
        }

        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(ws_pkt.payload);
            return ret;
        }

        ((uint8_t *)ws_pkt.payload)[ws_pkt.len] = '\0';
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WebSocket client disconnected: fd=%d", fd);
        removeClient(fd);

        if (_onClose) {
            _onClose(fd);
        }

        if (ws_pkt.payload) {
            free(ws_pkt.payload);
        }
        return ESP_OK;
    }

    if (_onFrame) {
        ret = _onFrame(req, &ws_pkt);
    }

    if (ws_pkt.payload) {
        free(ws_pkt.payload);
    }

    return ret;
}

void WebSocketServer::addClient(int fd) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    WebSocketClient client;
    client.fd = fd;
    client.last_seen = esp_timer_get_time();
    _clients[fd] = client;
    xSemaphoreGive(_mutex);
}

void WebSocketServer::removeClient(int fd) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _clients.erase(fd);
    xSemaphoreGive(_mutex);
}

WebSocketClient *WebSocketServer::getClient(int fd) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    auto it = _clients.find(fd);
    WebSocketClient *client = (it != _clients.end()) ? &it->second : nullptr;
    xSemaphoreGive(_mutex);
    return client;
}

esp_err_t WebSocketServer::sendText(int fd, const char *data, size_t len) {
    return sendBinary(fd, (const uint8_t *)data, len);
}

esp_err_t WebSocketServer::sendBinary(int fd, const uint8_t *data, size_t len) {
    if (!_server) {
        ESP_LOGE(TAG, "Server handle not set");
        return ESP_FAIL;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)data;
    ws_pkt.len = len;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    return httpd_ws_send_frame_async(_server, fd, &ws_pkt);
}

esp_err_t WebSocketServer::sendToAll(httpd_ws_type_t type, const uint8_t *data, size_t len) {
    if (!_server) {
        ESP_LOGE(TAG, "Server handle not set");
        return ESP_FAIL;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)data;
    ws_pkt.len = len;
    ws_pkt.type = type;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    for (auto &client_pair : _clients) {
        int fd = client_pair.first;
        httpd_ws_send_frame_async(_server, fd, &ws_pkt);
    }
    xSemaphoreGive(_mutex);

    return ESP_OK;
}

} // namespace websocket
