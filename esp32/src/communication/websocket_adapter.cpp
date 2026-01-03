#include <communication/websocket_adapter.h>

static const char* TAG = "Websocket";
static Websocket* g_ws_instance = nullptr;

Websocket::Websocket(httpd_handle_t* server_handle, const char* route) : server_handle_(server_handle), route_(route) {
    client_mutex_ = xSemaphoreCreateMutex();
    g_ws_instance = this;
}

void Websocket::begin() {
    httpd_uri_t ws_uri = {.uri = route_,
                          .method = HTTP_GET,
                          .handler = wsHandler,
                          .user_ctx = this,
                          .is_websocket = true,
                          .handle_ws_control_frames = true};
    httpd_register_uri_handler(*server_handle_, &ws_uri);
    ESP_LOGI(TAG, "WebSocket endpoint registered at %s", route_);
}

esp_err_t Websocket::wsHandler(httpd_req_t* req) {
    Websocket* self = static_cast<Websocket*>(req->user_ctx);

    if (req->method == HTTP_GET) {
        self->handleOpen(req);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = {};
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get frame len: %s", esp_err_to_name(ret));
        return ret;
    }

    if (ws_pkt.len == 0) {
        if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
            self->handleClose(httpd_req_to_sockfd(req));
        }
        return ESP_OK;
    }

    uint8_t* buf = (uint8_t*)malloc(ws_pkt.len);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for frame");
        return ESP_ERR_NO_MEM;
    }

    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to receive frame: %s", esp_err_to_name(ret));
        free(buf);
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_BINARY) {
        self->handleFrame(req, &ws_pkt);
    } else if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        self->handleClose(httpd_req_to_sockfd(req));
    }

    free(buf);
    return ESP_OK;
}

void Websocket::handleOpen(httpd_req_t* req) {
    int fd = httpd_req_to_sockfd(req);
    addClient(fd);
    ESP_LOGI(TAG, "Client connected [%d]", fd);
    sendPong(fd);
}

void Websocket::handleClose(int fd) {
    ESP_LOGI(TAG, "Client disconnected [%d]", fd);
    removeClientFromList(fd);
    removeClient(fd);
}

void Websocket::handleFrame(httpd_req_t* req, httpd_ws_frame_t* frame) {
    int fd = httpd_req_to_sockfd(req);
    handleIncoming(frame->payload, frame->len, fd);
}

void Websocket::addClient(int fd) {
    xSemaphoreTake(client_mutex_, portMAX_DELAY);
    clients_.push_back(fd);
    xSemaphoreGive(client_mutex_);
}

void Websocket::removeClientFromList(int fd) {
    xSemaphoreTake(client_mutex_, portMAX_DELAY);
    clients_.remove(fd);
    xSemaphoreGive(client_mutex_);
}

void Websocket::send(const uint8_t* data, size_t len, int cid) {
    if (!server_handle_ || !*server_handle_) return;

    httpd_ws_frame_t ws_pkt = {
        .final = true, .fragmented = false, .type = HTTPD_WS_TYPE_BINARY, .payload = (uint8_t*)data, .len = len};

    if (cid >= 0) {
        httpd_ws_send_frame_async(*server_handle_, cid, &ws_pkt);
    } else {
        xSemaphoreTake(client_mutex_, portMAX_DELAY);
        for (int fd : clients_) {
            httpd_ws_send_frame_async(*server_handle_, fd, &ws_pkt);
        }
        xSemaphoreGive(client_mutex_);
    }
}
