#include <mdns_service.h>

static const char *TAG = "MDNSService";

namespace mdns_service {

void begin(const char *hostname) {
    ESP_LOGI(TAG, "Starting mDNS with hostname: %s", hostname);

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %d", err);
        return;
    }

    err = mdns_hostname_set(hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set hostname: %d", err);
        return;
    }

    err = mdns_instance_name_set(hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set instance name: %d", err);
        return;
    }

    addService("http", "_tcp", 80);
    addService("ws", "_tcp", 80);
    addServiceTxt("http", "_tcp", "version", APP_VERSION);

    ESP_LOGI(TAG, "mDNS started successfully");
}

void end() { mdns_free(); }

void addService(const char *service, const char *proto, uint16_t port) {
    esp_err_t err = mdns_service_add(NULL, service, proto, port, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add service %s.%s: %d", service, proto, err);
    } else {
        ESP_LOGI(TAG, "Added mDNS service: %s.%s on port %d", service, proto, port);
    }
}

void addServiceTxt(const char *service, const char *proto, const char *key, const char *value) {
    mdns_txt_item_t txt_data[1] = {{(char *)key, (char *)value}};

    esp_err_t err = mdns_service_txt_set(service, proto, txt_data, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set TXT record for %s.%s: %d", service, proto, err);
    }
}

} // namespace mdns_service
