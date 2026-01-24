#pragma once

#include <esp_http_server.h>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <ArduinoJson.h>

using HttpGetHandler = std::function<esp_err_t(httpd_req_t*)>;
using HttpPostHandler = std::function<esp_err_t(httpd_req_t*, JsonVariant&)>;
using WsFrameHandler = std::function<esp_err_t(httpd_req_t*, httpd_ws_frame_t*)>;
using WsOpenHandler = std::function<void(httpd_req_t*)>;
using WsCloseHandler = std::function<void(int)>;

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
