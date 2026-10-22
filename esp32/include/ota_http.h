#pragma once

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <ota_download.h>

/**
 * An image over HTTPS through esp_http_client, its server checked against ESP-IDF's certificate bundle (the full
 * Mozilla list, CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL): a release asset on GitHub redirects to another host.
 */
class EspHttpSource : public OtaSource {
  public:
    int open(const char *url, int64_t &length) override {
        close();
        esp_http_client_config_t config = {};
        config.url = url;
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.timeout_ms = TIMEOUT_MS;
        config.keep_alive_enable = true;
        // A GitHub release asset redirects to a signed URL of about 900 characters, which the default 512-byte
        // buffers can neither take in from the Location header nor send back (Hexapod 933062c, found on its robot).
        config.buffer_size = 4096;
        config.buffer_size_tx = 2048;
        client_ = esp_http_client_init(&config);
        if (!client_) return 0;
        // As esp_https_ota does: open, and on a redirect follow it after draining the body that came with it.
        for (int redirects = 0; redirects <= MAX_REDIRECTS; redirects++) {
            esp_err_t err = esp_http_client_open(client_, 0);
            if (err != ESP_OK) {
                ESP_LOGW("OTA", "Download open failed: %s", esp_err_to_name(err));
                return 0;
            }
            length = esp_http_client_fetch_headers(client_);
            const int status = esp_http_client_get_status_code(client_);
            if (!redirect(status)) return status;
            if (esp_http_client_set_redirection(client_) != ESP_OK) return status;
            char drain[256];
            while (esp_http_client_read(client_, drain, sizeof(drain)) > 0) {
            }
        }
        return 0;
    }

    // A body that ends short of its Content-Length reads as its end; the session then finds the image incomplete.
    int read(uint8_t *buffer, size_t size) override {
        return esp_http_client_read(client_, reinterpret_cast<char *>(buffer), size);
    }

    void close() override {
        if (!client_) return;
        esp_http_client_close(client_);
        esp_http_client_cleanup(client_);
        client_ = nullptr;
    }

  private:
    static constexpr int TIMEOUT_MS = 15000;
    static constexpr int MAX_REDIRECTS = 5;

    static bool redirect(int status) {
        return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    }

    esp_http_client_handle_t client_ = nullptr;
};
