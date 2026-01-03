#include "www_mount.hpp"

static esp_err_t web_send(HttpRequest& req, const WebAsset& asset) {
    httpd_resp_set_status(req.raw, "200 OK");
    httpd_resp_set_type(req.raw, asset.mime);
    
    if (asset.gz) {
        httpd_resp_set_hdr(req.raw, "Content-Encoding", "gzip");
    }
    if (WWW_OPT.add_vary) {
        httpd_resp_set_hdr(req.raw, "Vary", "Accept-Encoding");
    }
    
    char cc[64];
    snprintf(cc, sizeof(cc), "public, immutable, max-age=%u", WWW_OPT.max_age);
    httpd_resp_set_hdr(req.raw, "Cache-Control", cc);
    
    char et[34];
    snprintf(et, sizeof(et), "\"%08x\"", asset.etag);
    httpd_resp_set_hdr(req.raw, "ETag", et);
    
    return httpd_resp_send(req.raw, (const char*)asset.data, asset.len);
}

void mountStaticAssets(HttpServer& server) {
    for (size_t i = 0; i < WWW_ASSETS_COUNT; i++) {
        const WebAsset* a = &WWW_ASSETS[i];
        server.on(a->uri, HTTP_GET, [a](HttpRequest& req) {
            return web_send(req, *a);
        });
    }
}
