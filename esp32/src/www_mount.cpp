#include <Arduino.h>
#include "www_mount.hpp"

static esp_err_t web_send(PsychicRequest* req, const WebAsset& asset) {
    PsychicResponse resp(req);
    resp.setCode(200);
    resp.setContentType(asset.mime);
    if (asset.gz) resp.addHeader("Content-Encoding", "gzip");
    if (WWW_OPT.add_vary) resp.addHeader("Vary", "Accept-Encoding");
    char cc[64];
    snprintf(cc, sizeof(cc), "public, immutable, max-age=%u", WWW_OPT.max_age);
    resp.addHeader("Cache-Control", cc);
    char et[34];
    snprintf(et, sizeof(et), "\"%08x\"", asset.etag);
    resp.addHeader("ETag", et);
    resp.setContent(asset.data, asset.len);
    return resp.send();
}

void mountStaticAssets(PsychicHttpServer& server) {
    static uint8_t buf[sizeof(PsychicWebHandler) * WWW_ASSETS_COUNT];
    for (size_t i = 0; i < WWW_ASSETS_COUNT; i++) {
        const WebAsset* a = &WWW_ASSETS[i];
        auto* handle = new (&buf[i * sizeof(PsychicWebHandler)]) PsychicWebHandler();
        handle->onRequest([a](PsychicRequest* req) { return web_send(req, *a); });
        server.on(a->uri, HTTP_GET, handle);
    }
    for (size_t i = 0; i < WWW_ASSETS_COUNT; i++) {
        if (strcmp(WWW_ASSETS[i].uri, WWW_OPT.default_uri) == 0) {
            server.defaultEndpoint->setHandler(
                reinterpret_cast<PsychicWebHandler*>(&buf[i * sizeof(PsychicWebHandler)]));
            break;
        }
    }
}