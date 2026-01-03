#pragma once

#include <Arduino.h>
#include <esp_http_server.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <platform_shared/message.pb.h>
#include <functional>
#include <vector>
#include <string>
#include <cstring>

#define HTTP_PROTO_BUFFER_SIZE 2048

class HttpRequest {
  public:
    httpd_req_t* raw;
    
    HttpRequest(httpd_req_t* req) : raw(req) {}
    
    std::string uri() const { return std::string(raw->uri); }
    
    size_t contentLength() const { return raw->content_len; }
    
    esp_err_t receiveBody(uint8_t* buf, size_t len) {
        int received = httpd_req_recv(raw, (char*)buf, len);
        return received == len ? ESP_OK : ESP_FAIL;
    }
    
    esp_err_t reply(int status) {
        httpd_resp_set_status(raw, status == 200 ? "200 OK" : 
                                   status == 202 ? "202 Accepted" :
                                   status == 400 ? "400 Bad Request" :
                                   status == 404 ? "404 Not Found" :
                                   status == 500 ? "500 Internal Server Error" : "200 OK");
        return httpd_resp_send(raw, nullptr, 0);
    }
    
    esp_err_t reply(int status, const char* contentType, const uint8_t* data, size_t len) {
        httpd_resp_set_status(raw, status == 200 ? "200 OK" : "500 Internal Server Error");
        httpd_resp_set_type(raw, contentType);
        return httpd_resp_send(raw, (const char*)data, len);
    }
    
    template <typename T>
    esp_err_t replyProto(const T& msg, const pb_msgdesc_t* fields) {
        uint8_t buffer[HTTP_PROTO_BUFFER_SIZE];
        pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
        if (!pb_encode(&stream, fields, &msg)) {
            return reply(500);
        }
        httpd_resp_set_type(raw, "application/x-protobuf");
        return httpd_resp_send(raw, (const char*)buffer, stream.bytes_written);
    }
    
    template <typename T>
    bool decodeProto(T& msg, const pb_msgdesc_t* fields) {
        size_t len = contentLength();
        if (len == 0 || len > HTTP_PROTO_BUFFER_SIZE) return false;
        
        uint8_t buffer[HTTP_PROTO_BUFFER_SIZE];
        if (receiveBody(buffer, len) != ESP_OK) return false;
        
        pb_istream_t stream = pb_istream_from_buffer(buffer, len);
        return pb_decode(&stream, fields, &msg);
    }
};

class HttpServer {
  public:
    HttpServer() : handle_(nullptr), max_uri_handlers_(50) {}
    
    void setMaxUriHandlers(size_t count) { max_uri_handlers_ = count; }
    
    esp_err_t begin(uint16_t port = 80) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = port;
        config.max_uri_handlers = max_uri_handlers_;
        config.stack_size = 8192;
        config.lru_purge_enable = true;
        config.uri_match_fn = httpd_uri_match_wildcard;
        
        esp_err_t ret = httpd_start(&handle_, &config);
        if (ret != ESP_OK) {
            ESP_LOGE("HttpServer", "Failed to start server: %s", esp_err_to_name(ret));
        }
        return ret;
    }
    
    void stop() {
        if (handle_) {
            httpd_stop(handle_);
            handle_ = nullptr;
        }
    }
    
    httpd_handle_t handle() { return handle_; }
    
    using RequestHandler = std::function<esp_err_t(HttpRequest&)>;
    
    esp_err_t on(const char* uri, httpd_method_t method, RequestHandler handler) {
        auto* ctx = new RequestHandler(std::move(handler));
        
        httpd_uri_t uri_handler = {
            .uri = uri,
            .method = method,
            .handler = [](httpd_req_t* req) -> esp_err_t {
                auto* handler = static_cast<RequestHandler*>(req->user_ctx);
                HttpRequest request(req);
                return (*handler)(request);
            },
            .user_ctx = ctx
        };
        
        return httpd_register_uri_handler(handle_, &uri_handler);
    }
    
    void addCorsHeaders() {
        cors_enabled_ = true;
    }
    
  private:
    httpd_handle_t handle_;
    size_t max_uri_handlers_;
    bool cors_enabled_ = false;
};

