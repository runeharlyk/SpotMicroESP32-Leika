#include <communication/webserver.h>
#include <communication/ws_origin.h>
#include <esp_log.h>
#include <cstring>
#include <algorithm>
#include <unistd.h>

static const char* TAG = "WebServer";

WebServer server;

WebServer::WebServer() {
    config_ = HTTPD_DEFAULT_CONFIG();
    wsMutex_ = xSemaphoreCreateMutex();
}

WebServer::~WebServer() {
    stop();
    vSemaphoreDelete(wsMutex_);
}

void WebServer::config(size_t maxUriHandlers, size_t stackSize) {
    config_.max_uri_handlers = maxUriHandlers;
    config_.stack_size = stackSize;
    config_.max_resp_headers = 16;
    config_.lru_purge_enable = true;
    config_.uri_match_fn = httpd_uri_match_wildcard;
    config_.global_user_ctx = this;
    config_.global_user_ctx_free_fn = keepContext;
    config_.close_fn = closeSession;
}

// Every session ends here, however it ended: a close frame, a dropped connection, or the least recently
// used one purged for a new client. A socket's subscriptions must end with it, or a reused descriptor
// would inherit them.
void WebServer::closeSession(httpd_handle_t handle, int sockfd) {
    static_cast<WebServer*>(httpd_get_global_user_ctx(handle))->dropWsClient(sockfd);
    close(sockfd);
}

esp_err_t WebServer::listen(uint16_t port) {
    config_.server_port = port;
    config_.ctrl_port = port + 32768;

    esp_err_t ret = httpd_start(&server_, &config_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
    }

    for (const HttpRoute& route : routes_) {
        esp_err_t err = registerRoute(route);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s (method %d): %s", route.uri.c_str(), route.method,
                     esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "Server started on port %d", port);
    return ESP_OK;
}

void WebServer::stop() {
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}

void WebServer::applyDefaultHeaders(httpd_req_t* req) {
    for (const auto& [key, value] : defaultHeaders_) {
        httpd_resp_set_hdr(req, key.c_str(), value.c_str());
    }
}

void WebServer::addDefaultHeader(const char* key, const char* value) { defaultHeaders_[key] = value; }

esp_err_t WebServer::httpHandler(httpd_req_t* req) {
    WebServer* self = static_cast<WebServer*>(req->user_ctx);
    self->applyDefaultHeaders(req);

    for (const auto& route : self->routes_) {
        if (route.isWebsocket) continue;

        bool uriMatch = false;
        if (route.uri.back() == '*') {
            std::string prefix = route.uri.substr(0, route.uri.length() - 1);
            uriMatch = strncmp(req->uri, prefix.c_str(), prefix.length()) == 0;
        } else {
            uriMatch = strcmp(req->uri, route.uri.c_str()) == 0;
        }

        if (uriMatch && route.method == req->method) {
            if (route.getHandler) {
                return route.getHandler(req);
            }
        }
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_FAIL;
}

#if !CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
#error "CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT is needed to refuse WebSockets from other pages"
#endif

// A header that is absent reads as nullptr; one too long to read refuses the socket.
static bool readHeader(httpd_req_t* req, const char* field, char* value, size_t size, const char*& read) {
    esp_err_t err = httpd_req_get_hdr_value_str(req, field, value, size);
    read = err == ESP_OK ? value : nullptr;
    return err == ESP_OK || err == ESP_ERR_NOT_FOUND;
}

esp_err_t WebServer::wsPreHandshake(httpd_req_t* req) {
    char originValue[128];
    char hostValue[128];
    const char* origin;
    const char* host;
    if (readHeader(req, "Origin", originValue, sizeof(originValue), origin) &&
        readHeader(req, "Host", hostValue, sizeof(hostValue), host) && wsOriginAllowed(origin, host)) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Refused a WebSocket opened by %s", origin ? origin : "an unreadable origin");
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "This page may not open the robot's socket");
    return ESP_FAIL;
}

esp_err_t WebServer::wsHandler(httpd_req_t* req) {
    WebServer* self = static_cast<WebServer*>(req->user_ctx);

    if (req->method == HTTP_GET) {
        int sockfd = httpd_req_to_sockfd(req);
        self->addWsClient(sockfd);
        if (self->wsOpenHandler_) {
            self->wsOpenHandler_(req);
        }
        ESP_LOGI(TAG, "WebSocket client connected: %d", sockfd);
        return ESP_OK;
    }

    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(httpd_ws_frame_t));
    frame.type = HTTPD_WS_TYPE_BINARY;

    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get frame len: %s", esp_err_to_name(ret));
        return ret;
    }

    if (frame.len > 0) {
        frame.payload = (uint8_t*)malloc(frame.len);
        if (!frame.payload) {
            ESP_LOGE(TAG, "Failed to allocate frame payload");
            return ESP_ERR_NO_MEM;
        }

        ret = httpd_ws_recv_frame(req, &frame, frame.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to receive frame: %s", esp_err_to_name(ret));
            free(frame.payload);
            return ret;
        }
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        self->dropWsClient(httpd_req_to_sockfd(req));
        if (frame.payload) free(frame.payload);
        return ESP_OK;
    }

    esp_err_t result = ESP_OK;
    if (self->wsFrameHandler_) {
        result = self->wsFrameHandler_(req, &frame);
    }

    if (frame.payload) {
        free(frame.payload);
    }

    return result;
}

void WebServer::on(const char* uri, httpd_method_t method, HttpGetHandler handler) {
    addRoute({uri, method, handler, false});
}

// The server's task reads the routes while it runs, so they are all added before listen().
void WebServer::addRoute(HttpRoute route) {
    if (server_) {
        ESP_LOGE(TAG, "Refused %s: routes are added before listen()", route.uri.c_str());
        return;
    }
    routes_.push_back(std::move(route));
}

esp_err_t WebServer::registerRoute(const HttpRoute& route) {
    httpd_uri_t httpd_route = {.uri = route.uri.c_str(),
                               .method = route.method,
                               .handler = route.isWebsocket ? wsHandler : httpHandler,
                               .user_ctx = this,
                               .is_websocket = route.isWebsocket,
                               .handle_ws_control_frames = route.isWebsocket,
                               .supported_subprotocol = nullptr,
                               .ws_pre_handshake_cb = route.isWebsocket ? wsPreHandshake : nullptr};
    return httpd_register_uri_handler(server_, &httpd_route);
}

void WebServer::registerWebsocket(const char* uri) { addRoute({uri, HTTP_GET, nullptr, true}); }

void WebServer::onWsFrame(WsFrameHandler handler) { wsFrameHandler_ = handler; }

void WebServer::onWsOpen(WsOpenHandler handler) { wsOpenHandler_ = handler; }

void WebServer::onWsClose(WsCloseHandler handler) { wsCloseHandler_ = handler; }

void WebServer::addWsClient(int sockfd) {
    xSemaphoreTake(wsMutex_, portMAX_DELAY);
    wsClients_.push_back(sockfd);
    xSemaphoreGive(wsMutex_);
}

// Reports a WebSocket client's end once, whichever of its close frame and its session's end comes first.
void WebServer::dropWsClient(int sockfd) {
    xSemaphoreTake(wsMutex_, portMAX_DELAY);
    auto client = std::find(wsClients_.begin(), wsClients_.end(), sockfd);
    bool wasClient = client != wsClients_.end();
    if (wasClient) wsClients_.erase(client);
    xSemaphoreGive(wsMutex_);
    if (wasClient && wsCloseHandler_) wsCloseHandler_(sockfd);
}

esp_err_t WebServer::wsSend(int sockfd, const uint8_t* data, size_t len) {
    httpd_ws_frame_t frame = {.final = true,
                              .fragmented = false,
                              .type = HTTPD_WS_TYPE_BINARY,
                              .payload = const_cast<uint8_t*>(data),
                              .len = len};
    return httpd_ws_send_frame_async(server_, sockfd, &frame);
}

esp_err_t WebServer::sendError(httpd_req_t* req, int status, const char* message) {
    return send(req, status, (uint8_t*)message, strlen(message));
}

esp_err_t WebServer::sendOk(httpd_req_t* req) { return send(req, 200, nullptr, 0); }

esp_err_t WebServer::send(httpd_req_t* req, int status, const uint8_t* data, size_t len) {
    httpd_resp_set_status(req, status == 200   ? "200 OK"
                               : status == 202 ? "202 Accepted"
                               : status == 400 ? "400 Bad Request"
                               : status == 404 ? "404 Not Found"
                               : status == 500 ? "500 Internal Server Error"
                                               : "200 OK");
    httpd_resp_set_type(req, "application/x-protobuf");
    return httpd_resp_send(req, (const char*)data, len);
}
