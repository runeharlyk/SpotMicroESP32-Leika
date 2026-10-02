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

using HttpGetHandler = std::function<esp_err_t(httpd_req_t*)>;
using WsFrameHandler = std::function<esp_err_t(httpd_req_t*, httpd_ws_frame_t*)>;
using WsOpenHandler = std::function<void(httpd_req_t*)>;
using WsCloseHandler = std::function<void(int)>;

struct HttpRoute {
    std::string uri;
    httpd_method_t method;
    HttpGetHandler getHandler;
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

    void onWsFrame(WsFrameHandler handler);
    void onWsOpen(WsOpenHandler handler);
    void onWsClose(WsCloseHandler handler);
    void registerWebsocket(const char* uri);

    esp_err_t wsSend(int sockfd, const uint8_t* data, size_t len);

    void addDefaultHeader(const char* key, const char* value);

    /** Runs `work` on the server's task between requests and frames; false when it could not be queued. */
    bool queueWork(std::function<void()> work);

    /**
     * Waits up to `ms` for a socket to take a write without blocking. lwIP reports a socket writable once
     * TCP_SNDLOWAT bytes are free, half its send buffer (CONFIG_LWIP_TCP_SND_BUF_DEFAULT).
     */
    static bool waitWritable(int sockfd, uint32_t ms);

    static esp_err_t sendError(httpd_req_t* req, int status, const char* message);
    static esp_err_t sendOk(httpd_req_t* req);
    static esp_err_t send(httpd_req_t* req, int status, const uint8_t* data, size_t len);

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
    static esp_err_t wsPreHandshake(httpd_req_t* req);
    static esp_err_t openSession(httpd_handle_t handle, int sockfd);
    static void closeSession(httpd_handle_t handle, int sockfd);
    static void keepContext(void*) {}

    void addWsClient(int sockfd);
    void dropWsClient(int sockfd);

    void applyDefaultHeaders(httpd_req_t* req);
    void addRoute(HttpRoute route);
    esp_err_t registerRoute(const HttpRoute& route);
};

extern WebServer server;
