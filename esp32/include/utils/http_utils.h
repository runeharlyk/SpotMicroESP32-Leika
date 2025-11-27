#pragma once

#include <esp_http_server.h>
#include <ArduinoJson.h>

namespace http_utils {

esp_err_t send_json_response(httpd_req_t *req, JsonDocument &doc, int status_code = 200);

esp_err_t send_error(httpd_req_t *req, int status_code, const char *message = nullptr);

esp_err_t send_empty_response(httpd_req_t *req, int status_code = 200);

esp_err_t add_cors_headers(httpd_req_t *req);

esp_err_t add_standard_headers(httpd_req_t *req);

esp_err_t parse_json_body(httpd_req_t *req, JsonDocument &doc);

esp_err_t handle_options_cors(httpd_req_t *req);

const char *get_client_ip(httpd_req_t *req);

} // namespace http_utils
