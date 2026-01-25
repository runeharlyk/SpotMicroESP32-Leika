#pragma once

#include <esp_http_server.h>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <ArduinoJson.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <platform_shared/api.pb.h>

using HttpGetHandler = std::function<esp_err_t(httpd_req_t*)>;
using HttpPostHandler = std::function<esp_err_t(httpd_req_t*, JsonVariant&)>;
using HttpProtoHandler = std::function<esp_err_t(httpd_req_t*, api_Request*)>;
using WsFrameHandler = std::function<esp_err_t(httpd_req_t*, httpd_ws_frame_t*)>;
using WsOpenHandler = std::function<void(httpd_req_t*)>;
using WsCloseHandler = std::function<void(int)>;

// Macro to register a proto endpoint that extracts a specific payload type
// Usage: PROTO_ENDPOINT(server, "/api/files/delete", file_delete_request, FileSystem::handleDelete)
// Handler signature: esp_err_t handleDelete(httpd_req_t* req, const api_FileDeleteRequest& payload)
#define PROTO_ENDPOINT(server_ref, uri, payload_type, handler) \
    (server_ref).onProto(uri, HTTP_POST, [&](httpd_req_t *request, api_Request *protoReq) { \
        if (protoReq->which_payload != api_Request_##payload_type##_tag) { \
            return WebServer::sendError(request, 400, "Invalid request payload"); \
        } \
        return handler(request, protoReq->payload.payload_type); \
    })

struct HttpRoute {
    std::string uri;
    httpd_method_t method;
    HttpGetHandler getHandler;
    HttpPostHandler postHandler;
    HttpProtoHandler protoHandler;  // For proto handlers that don't need JSON parsing
    bool isWebsocket;
};

class WebServer {
  public:
    WebServer();
    ~WebServer();

    void config(size_t maxUriHandlers, size_t stackSize);
    esp_err_t listen(uint16_t port);
    void stop();

    void on(const char* uri, httpd_method_t method, HttpGetHandler handler);
    void on(const char* uri, httpd_method_t method, HttpPostHandler handler);
    void onProto(const char* uri, httpd_method_t method, HttpProtoHandler handler);

    void onWsFrame(WsFrameHandler handler);
    void onWsOpen(WsOpenHandler handler);
    void onWsClose(WsCloseHandler handler);
    void registerWebsocket(const char* uri);

    esp_err_t wsSend(int sockfd, const uint8_t* data, size_t len);
    esp_err_t wsSendAll(const uint8_t* data, size_t len);
    void addWsClient(int sockfd);
    void removeWsClient(int sockfd);
    std::vector<int> getWsClients();

    void addDefaultHeader(const char* key, const char* value);

    httpd_handle_t getHandle() { return server_; }

    static esp_err_t sendJson(httpd_req_t* req, int status, const char* json);
    static esp_err_t sendJson(httpd_req_t* req, int status, JsonDocument& doc);
    static esp_err_t sendError(httpd_req_t* req, int status, const char* message);
    static esp_err_t sendOk(httpd_req_t* req);
    static esp_err_t sendProto(httpd_req_t* req, int status, const uint8_t* data, size_t len);

    template <typename T>
    static esp_err_t sendProto(httpd_req_t* req, int status, const T& msg, const pb_msgdesc_t* fields) {
        uint8_t buffer[1024];
        pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
        if (!pb_encode(&stream, fields, &msg)) {
            return sendError(req, 500, "Failed to encode proto");
        }
        return sendProto(req, status, buffer, stream.bytes_written);
    }

    template <typename T>
    static bool receiveProto(httpd_req_t* req, T& msg, const pb_msgdesc_t* fields) {
        size_t contentLen = req->content_len;
        if (contentLen == 0 || contentLen > 4096) {
            return false;
        }
        uint8_t* buffer = (uint8_t*)malloc(contentLen);
        if (!buffer) {
            return false;
        }
        int received = 0;
        int remaining = contentLen;
        while (remaining > 0) {
            int ret = httpd_req_recv(req, (char*)buffer + received, remaining);
            if (ret <= 0) {
                free(buffer);
                return false;
            }
            received += ret;
            remaining -= ret;
        }
        pb_istream_t stream = pb_istream_from_buffer(buffer, contentLen);
        bool success = pb_decode(&stream, fields, &msg);
        free(buffer);
        return success;
    }

  private:
    httpd_handle_t server_ = nullptr;
    httpd_config_t config_;
    std::vector<HttpRoute> routes_;
    std::map<std::string, std::string> defaultHeaders_;
    std::vector<int> wsClients_;
    SemaphoreHandle_t wsMutex_;

    WsFrameHandler wsFrameHandler_;
    WsOpenHandler wsOpenHandler_;
    WsCloseHandler wsCloseHandler_;

    static esp_err_t httpHandler(httpd_req_t* req);
    static esp_err_t wsHandler(httpd_req_t* req);

    void applyDefaultHeaders(httpd_req_t* req);
    esp_err_t registerRoute(const HttpRoute& route);
};

extern WebServer server;
