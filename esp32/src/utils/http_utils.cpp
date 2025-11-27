#include <utils/http_utils.h>
#include <global.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace http_utils {

static const char *TAG = "HttpUtils";

esp_err_t send_json_response(httpd_req_t *req, JsonDocument &doc, int status_code) {
    add_standard_headers(req);
    add_cors_headers(req);

    httpd_resp_set_type(req, "application/json");

    if (status_code != 200) {
        char status_str[4];
        snprintf(status_str, sizeof(status_str), "%d", status_code);
        httpd_resp_set_status(req, status_str);
    }

    String json_str;
    serializeJson(doc, json_str);

    return httpd_resp_send(req, json_str.c_str(), json_str.length());
}

esp_err_t send_error(httpd_req_t *req, int status_code, const char *message) {
    add_standard_headers(req);
    add_cors_headers(req);

    char status_str[4];
    snprintf(status_str, sizeof(status_str), "%d", status_code);
    httpd_resp_set_status(req, status_str);

    if (message) {
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, message, strlen(message));
    }

    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t send_empty_response(httpd_req_t *req, int status_code) {
    add_standard_headers(req);
    add_cors_headers(req);

    if (status_code != 200) {
        char status_str[4];
        snprintf(status_str, sizeof(status_str), "%d", status_code);
        httpd_resp_set_status(req, status_str);
    }

    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t add_cors_headers(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Accept, Content-Type, Authorization");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
    return ESP_OK;
}

esp_err_t add_standard_headers(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Server", APP_NAME);
    return ESP_OK;
}

esp_err_t parse_json_body(httpd_req_t *req, JsonDocument &doc) {
    size_t content_len = req->content_len;

    if (content_len == 0) {
        ESP_LOGW(TAG, "Empty request body");
        return ESP_FAIL;
    }

    if (content_len > 16384) {
        ESP_LOGE(TAG, "Request body too large: %d bytes", content_len);
        return ESP_FAIL;
    }

    char *buf = (char *)malloc(content_len + 1);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for request body");
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, buf, content_len);
    if (ret <= 0) {
        free(buf);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "Socket timeout");
            return ESP_ERR_TIMEOUT;
        }
        return ESP_FAIL;
    }

    buf[ret] = '\0';

    DeserializationError error = deserializeJson(doc, buf, ret);
    free(buf);

    if (error) {
        ESP_LOGE(TAG, "JSON parse error: %s", error.c_str());
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t handle_options_cors(httpd_req_t *req) { return send_empty_response(req, 200); }

const char *get_client_ip(httpd_req_t *req) {
    int sockfd = httpd_req_to_sockfd(req);
    struct sockaddr_in6 addr;
    socklen_t addr_size = sizeof(addr);

    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_size) < 0) {
        return "unknown";
    }

    static char ip_str[INET6_ADDRSTRLEN];
    if (addr.sin6_family == AF_INET) {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)&addr;
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", (addr_in->sin_addr.s_addr >> 0) & 0xFF,
                 (addr_in->sin_addr.s_addr >> 8) & 0xFF, (addr_in->sin_addr.s_addr >> 16) & 0xFF,
                 (addr_in->sin_addr.s_addr >> 24) & 0xFF);
    } else {
        inet_ntop(AF_INET6, &addr.sin6_addr, ip_str, sizeof(ip_str));
    }

    return ip_str;
}

} // namespace http_utils
