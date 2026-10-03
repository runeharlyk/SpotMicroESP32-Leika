#include "www_mount.hpp"
#include <cstring>

static const char* TAG = "WebApp";

static esp_err_t sendNotFound(httpd_req_t* req) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_FAIL;
}

#include "WWWData.h"

static const WebAsset* findAsset(const char* uri) {
    for (size_t i = 0; i < WWW_ASSETS_COUNT; i++) {
        if (strcmp(WWW_ASSETS[i].uri, uri) == 0) {
            return &WWW_ASSETS[i];
        }
    }
    return nullptr;
}

// The app's files keep their names across firmware updates (bundle.js, index.html), so browsers must
// revalidate them on every load; an unchanged file costs an empty 304 thanks to its content ETag.
static esp_err_t web_send(httpd_req_t* req, const WebAsset& asset) {
    char etag[12];
    snprintf(etag, sizeof(etag), "\"%08lx\"", (unsigned long)asset.etag);
    httpd_resp_set_hdr(req, "ETag", etag);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    if (WWW_OPT.add_vary) httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");

    char known[sizeof(etag)];
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", known, sizeof(known)) == ESP_OK &&
        strcmp(known, etag) == 0) {
        httpd_resp_set_status(req, "304 Not Modified");
        return httpd_resp_send(req, nullptr, 0);
    }

    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, asset.mime);
    if (asset.gz) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char*)asset.data, asset.len);
}

size_t webAssetCount() { return WWW_ASSETS_COUNT; }

void mountWebApp(WebServer& server) {
    ESP_LOGI(TAG, "Mounting %u embedded web assets", (unsigned)WWW_ASSETS_COUNT);
    for (size_t i = 0; i < WWW_ASSETS_COUNT; i++) {
        const WebAsset* a = &WWW_ASSETS[i];
        server.on(a->uri, HTTP_GET, [a](httpd_req_t* req) { return web_send(req, *a); });
    }

    const WebAsset* indexAsset = findAsset(WWW_OPT.default_uri);
    if (!indexAsset) {
        ESP_LOGE(TAG, "Embedded web app has no %s, UI routes will return 404", WWW_OPT.default_uri);
    }
    server.on("/*", HTTP_GET, [indexAsset](httpd_req_t* req) {
        if (!indexAsset || strncmp(req->uri, "/api/", 5) == 0) return sendNotFound(req);
        return web_send(req, *indexAsset);
    });
}
