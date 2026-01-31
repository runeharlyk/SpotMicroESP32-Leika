#include "www_mount.hpp"
#include <cstring>

static const WebAsset* findAsset(const char* uri) {
    for (size_t i = 0; i < WWW_ASSETS_COUNT; i++) {
        if (strcmp(WWW_ASSETS[i].uri, uri) == 0) {
            return &WWW_ASSETS[i];
        }
    }
    return nullptr;
}

static esp_err_t web_send(httpd_req_t* req, const WebAsset& asset) {
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, asset.mime);
    if (asset.gz) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    if (WWW_OPT.add_vary) httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");

    char cc[64];
    snprintf(cc, sizeof(cc), "public, immutable, max-age=%lu", (unsigned long)WWW_OPT.max_age);
    httpd_resp_set_hdr(req, "Cache-Control", cc);

    char et[34];
    snprintf(et, sizeof(et), "\"%08lx\"", (unsigned long)asset.etag);
    httpd_resp_set_hdr(req, "ETag", et);

    return httpd_resp_send(req, (const char*)asset.data, asset.len);
}

void mountStaticAssets(WebServer& server) {
    for (size_t i = 0; i < WWW_ASSETS_COUNT; i++) {
        const WebAsset* a = &WWW_ASSETS[i];
        server.on(a->uri, HTTP_GET, [a](httpd_req_t* req) { return web_send(req, *a); });
    }
}

void mountSpaFallback(WebServer& server) {
    const WebAsset* indexAsset = findAsset(WWW_OPT.default_uri);
    if (indexAsset) {
        server.on("/*", HTTP_GET, [indexAsset](httpd_req_t* req) {
            if (strncmp(req->uri, "/api/", 5) == 0) {
                httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
                return ESP_FAIL;
            }
            return web_send(req, *indexAsset);
        });
    }
}
