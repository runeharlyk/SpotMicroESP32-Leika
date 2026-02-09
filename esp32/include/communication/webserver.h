#pragma once

#ifndef CONFIG_HTTPD_WS_SUPPORT
#define CONFIG_HTTPD_WS_SUPPORT 1
#endif

#include <esp_http_server.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <pb_encode.h>
#include <pb_decode.h>
#include <platform_shared/api.pb.h>
#include <eventbus.hpp>

using HttpGetHandler = std::function<esp_err_t(httpd_req_t*)>;
using HttpPostHandler = std::function<esp_err_t(httpd_req_t*, api_Request*)>;
using WsFrameHandler = std::function<esp_err_t(httpd_req_t*, httpd_ws_frame_t*)>;
using WsOpenHandler = std::function<void(httpd_req_t*)>;
using WsCloseHandler = std::function<void(int)>;

#define PROTO_ROUTE(server_ref, uri, field_name, proto_type)               \
    (server_ref)                                                           \
        .protoRoute<proto_type>(                                           \
            uri,                                                           \
            [](const api_Request& req, proto_type& out) -> bool {          \
                if (req.which_payload == api_Request_##field_name##_tag) { \
                    out = req.payload.field_name;                          \
                    return true;                                           \
                }                                                          \
                return false;                                              \
            },                                                             \
            [](api_Response& res, const proto_type& data) {                \
                res.which_payload = api_Response_##field_name##_tag;       \
                res.payload.field_name = data;                             \
            })

struct HttpRoute {
    std::string uri;
    httpd_method_t method;
    HttpGetHandler getHandler;
    HttpPostHandler postHandler;
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

    template <typename ProtoT>
    void protoRoute(const char* uri, std::function<bool(const api_Request&, ProtoT&)> extractor,
                    std::function<void(api_Response&, const ProtoT&)> assigner) {
        on(uri, HTTP_GET, [assigner](httpd_req_t* req) {
            auto* res = new api_Response();
            *res = api_Response_init_zero;
            res->status_code = 200;
            ProtoT current = EventBus::instance().peek<ProtoT>();
            assigner(*res, current);
            esp_err_t ret = WebServer::send(req, 200, *res, api_Response_fields);
            delete res;
            return ret;
        });
        on(uri, HTTP_POST, [extractor, assigner](httpd_req_t* req, api_Request* protoReq) {
            ProtoT msg = {};
            if (!extractor(*protoReq, msg)) {
                return sendError(req, 400, "Invalid request type");
            }
            EventBus::instance().publish(msg);
            auto* res = new api_Response();
            *res = api_Response_init_zero;
            res->status_code = 200;
            ProtoT current = EventBus::instance().peek<ProtoT>();
            assigner(*res, current);
            esp_err_t ret = WebServer::send(req, 200, *res, api_Response_fields);
            delete res;
            return ret;
        });
    }

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

    static esp_err_t sendError(httpd_req_t* req, int status, const char* message);
    static esp_err_t sendOk(httpd_req_t* req);
    static esp_err_t send(httpd_req_t* req, int status, const uint8_t* data, size_t len);

    template <typename T>
    static esp_err_t send(httpd_req_t* req, int status, const T& msg, const pb_msgdesc_t* fields) {
        size_t size = 0;
        if (!pb_get_encoded_size(&size, fields, &msg)) {
            return sendError(req, 500, "Failed to calculate proto size");
        }

        uint8_t* buffer = (uint8_t*)malloc(size);
        if (!buffer) {
            return sendError(req, 500, "Failed to allocate memory for proto");
        }

        pb_ostream_t stream = pb_ostream_from_buffer(buffer, size);
        if (!pb_encode(&stream, fields, &msg)) {
            free(buffer);
            return sendError(req, 500, "Failed to encode proto");
        }

        esp_err_t result = send(req, status, buffer, stream.bytes_written);
        free(buffer);
        return result;
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
