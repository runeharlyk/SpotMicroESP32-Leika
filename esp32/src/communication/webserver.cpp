#include <communication/webserver.h>
#include <esp_log.h>
#include <cstring>
#include <algorithm>

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
}

esp_err_t WebServer::listen(uint16_t port) {
    config_.server_port = port;
    config_.ctrl_port = port + 32768;

    esp_err_t ret = httpd_start(&server_, &config_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
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
            if (route.protoHandler) {
                size_t contentLen = req->content_len;
                if (contentLen == 0 || contentLen > 4096) {
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
                    return ESP_FAIL;
                }

                uint8_t* buffer = (uint8_t*)malloc(contentLen);
                if (!buffer) {
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
                    return ESP_FAIL;
                }

                int received = 0;
                int remaining = contentLen;
                while (remaining > 0) {
                    int ret = httpd_req_recv(req, (char*)buffer + received, remaining);
                    if (ret <= 0) {
                        free(buffer);
                        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Request timeout");
                        }
                        return ESP_FAIL;
                    }
                    received += ret;
                    remaining -= ret;
                }

                api_Request protoReq = api_Request_init_zero;
                pb_istream_t stream = pb_istream_from_buffer(buffer, contentLen);
                bool success = pb_decode(&stream, api_Request_fields, &protoReq);
                free(buffer);

                if (!success) {
                    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to decode protobuf");
                    return ESP_FAIL;
                }

                return route.protoHandler(req, &protoReq);
            }
            if (route.postHandler) {
                char* content = nullptr;
                size_t contentLen = req->content_len;

                if (contentLen > 0) {
                    content = (char*)malloc(contentLen + 1);
                    if (!content) {
                        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
                        return ESP_FAIL;
                    }

                    int received = 0;
                    int remaining = contentLen;
                    while (remaining > 0) {
                        int ret = httpd_req_recv(req, content + received, remaining);
                        if (ret <= 0) {
                            free(content);
                            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                                httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Request timeout");
                            }
                            return ESP_FAIL;
                        }
                        received += ret;
                        remaining -= ret;
                    }
                    content[contentLen] = '\0';
                }

                JsonDocument doc;
                if (content && contentLen > 0) {
                    DeserializationError error = deserializeJson(doc, content, contentLen);
                    free(content);
                    if (error) {
                        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
                        return ESP_FAIL;
                    }
                }

                JsonVariant json = doc.as<JsonVariant>();
                return route.postHandler(req, json);
            }
        }
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
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
        int sockfd = httpd_req_to_sockfd(req);
        self->removeWsClient(sockfd);
        if (self->wsCloseHandler_) {
            self->wsCloseHandler_(sockfd);
        }
        ESP_LOGI(TAG, "WebSocket client disconnected: %d", sockfd);
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
    HttpRoute route;
    route.uri = uri;
    route.method = method;
    route.getHandler = handler;
    route.postHandler = nullptr;
    route.protoHandler = nullptr;
    route.isWebsocket = false;
    routes_.push_back(route);

    if (server_) {
        registerRoute(route);
    }
}

void WebServer::on(const char* uri, httpd_method_t method, HttpPostHandler handler) {
    HttpRoute route;
    route.uri = uri;
    route.method = method;
    route.getHandler = nullptr;
    route.postHandler = handler;
    route.protoHandler = nullptr;
    route.isWebsocket = false;
    routes_.push_back(route);

    if (server_) {
        registerRoute(route);
    }
}

void WebServer::onProto(const char* uri, httpd_method_t method, HttpProtoHandler handler) {
    HttpRoute route;
    route.uri = uri;
    route.method = method;
    route.getHandler = nullptr;
    route.postHandler = nullptr;
    route.protoHandler = handler;
    route.isWebsocket = false;
    routes_.push_back(route);

    if (server_) {
        registerRoute(route);
    }
}

esp_err_t WebServer::registerRoute(const HttpRoute& route) {
    httpd_uri_t httpd_route = {.uri = route.uri.c_str(),
                               .method = route.method,
                               .handler = route.isWebsocket ? wsHandler : httpHandler,
                               .user_ctx = this,
                               .is_websocket = route.isWebsocket,
                               .handle_ws_control_frames = route.isWebsocket,
                               .supported_subprotocol = nullptr};
    return httpd_register_uri_handler(server_, &httpd_route);
}

void WebServer::registerWebsocket(const char* uri) {
    HttpRoute route;
    route.uri = uri;
    route.method = HTTP_GET;
    route.getHandler = nullptr;
    route.postHandler = nullptr;
    route.protoHandler = nullptr;
    route.isWebsocket = true;
    routes_.push_back(route);

    if (server_) {
        registerRoute(route);
    }
}

void WebServer::onWsFrame(WsFrameHandler handler) { wsFrameHandler_ = handler; }

void WebServer::onWsOpen(WsOpenHandler handler) { wsOpenHandler_ = handler; }

void WebServer::onWsClose(WsCloseHandler handler) { wsCloseHandler_ = handler; }

void WebServer::addWsClient(int sockfd) {
    xSemaphoreTake(wsMutex_, portMAX_DELAY);
    wsClients_.push_back(sockfd);
    xSemaphoreGive(wsMutex_);
}

void WebServer::removeWsClient(int sockfd) {
    xSemaphoreTake(wsMutex_, portMAX_DELAY);
    wsClients_.erase(std::remove(wsClients_.begin(), wsClients_.end(), sockfd), wsClients_.end());
    xSemaphoreGive(wsMutex_);
}

std::vector<int> WebServer::getWsClients() {
    xSemaphoreTake(wsMutex_, portMAX_DELAY);
    std::vector<int> clients = wsClients_;
    xSemaphoreGive(wsMutex_);
    return clients;
}

esp_err_t WebServer::wsSend(int sockfd, const uint8_t* data, size_t len) {
    httpd_ws_frame_t frame = {.final = true,
                              .fragmented = false,
                              .type = HTTPD_WS_TYPE_BINARY,
                              .payload = const_cast<uint8_t*>(data),
                              .len = len};
    return httpd_ws_send_frame_async(server_, sockfd, &frame);
}

esp_err_t WebServer::wsSendAll(const uint8_t* data, size_t len) {
    xSemaphoreTake(wsMutex_, portMAX_DELAY);
    for (int sockfd : wsClients_) {
        wsSend(sockfd, data, len);
    }
    xSemaphoreGive(wsMutex_);
    return ESP_OK;
}

esp_err_t WebServer::sendJson(httpd_req_t* req, int status, const char* json) {
    httpd_resp_set_status(req, status == 200   ? "200 OK"
                               : status == 400 ? "400 Bad Request"
                               : status == 404 ? "404 Not Found"
                               : status == 500 ? "500 Internal Server Error"
                                               : "200 OK");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, strlen(json));
}

esp_err_t WebServer::sendJson(httpd_req_t* req, int status, JsonDocument& doc) {
    std::string json;
    serializeJson(doc, json);
    return sendJson(req, status, json.c_str());
}

esp_err_t WebServer::sendError(httpd_req_t* req, int status, const char* message) {
    JsonDocument doc;
    doc["error"] = message;
    return sendJson(req, status, doc);
}

esp_err_t WebServer::sendOk(httpd_req_t* req) { return sendJson(req, 200, "{\"status\":\"ok\"}"); }

esp_err_t WebServer::sendProto(httpd_req_t* req, int status, const uint8_t* data, size_t len) {
    httpd_resp_set_status(req, status == 200   ? "200 OK"
                               : status == 400 ? "400 Bad Request"
                               : status == 404 ? "404 Not Found"
                               : status == 500 ? "500 Internal Server Error"
                                               : "200 OK");
    httpd_resp_set_type(req, "application/x-protobuf");
    return httpd_resp_send(req, (const char*)data, len);
}
