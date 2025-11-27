#include "www_mount.hpp"
#include <esp_log.h>
#include <string.h>

static const char *TAG = "WWWMount";

struct asset_handler_ctx {
    const WebAsset *asset;
};

static esp_err_t web_send(httpd_req_t *req, const WebAsset &asset) {
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, asset.mime);

    if (asset.gz) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

    if (WWW_OPT.add_vary) {
        httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
    }

    char cc[64];
    snprintf(cc, sizeof(cc), "public, immutable, max-age=%u", WWW_OPT.max_age);
    httpd_resp_set_hdr(req, "Cache-Control", cc);

    char et[34];
    snprintf(et, sizeof(et), "\"%08x\"", asset.etag);
    httpd_resp_set_hdr(req, "ETag", et);

    return httpd_resp_send(req, (const char *)asset.data, asset.len);
}

static esp_err_t asset_handler(httpd_req_t *req) {
    asset_handler_ctx *ctx = (asset_handler_ctx *)req->user_ctx;
    return web_send(req, *ctx->asset);
}

static esp_err_t default_handler(httpd_req_t *req) {
    for (size_t i = 0; i < WWW_ASSETS_COUNT; i++) {
        if (strcmp(WWW_ASSETS[i].uri, WWW_OPT.default_uri) == 0) {
            return web_send(req, WWW_ASSETS[i]);
        }
    }
    httpd_resp_send_404(req);
    return ESP_OK;
}

void mountStaticAssets(httpd_handle_t server) {
    static asset_handler_ctx contexts[WWW_ASSETS_COUNT];

    for (size_t i = 0; i < WWW_ASSETS_COUNT; i++) {
        contexts[i].asset = &WWW_ASSETS[i];

        httpd_uri_t uri_handler = {
            .uri = WWW_ASSETS[i].uri, .method = HTTP_GET, .handler = asset_handler, .user_ctx = &contexts[i]};

        esp_err_t err = httpd_register_uri_handler(server, &uri_handler);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register handler for %s: %d", WWW_ASSETS[i].uri, err);
        } else {
            ESP_LOGD(TAG, "Registered handler for %s", WWW_ASSETS[i].uri);
        }
    }

    httpd_uri_t default_uri_handler = {.uri = "/", .method = HTTP_GET, .handler = default_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(server, &default_uri_handler);

    ESP_LOGI(TAG, "Mounted %d static assets", WWW_ASSETS_COUNT);
}
