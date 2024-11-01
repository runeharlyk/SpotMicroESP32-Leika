#include <firmware_download_service.h>

static const char *TAG = "Download OAT";

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_src_certs_x509_crt_bundle_bin_start");

static int previousProgress = 0;
JsonDocument doc;
char output[128];

void update_started() {
    ESP_LOGI(TAG, "Update started");
    doc["status"] = "preparing";
    serializeJson(doc, output);
    socket.emit(EVENT_DOWNLOAD_OTA, output);
}

void update_progress(int currentBytes, int totalBytes) {
    doc["status"] = "progress";
    int progress = ((currentBytes * 100) / totalBytes);
    if (progress > previousProgress) {
        doc["progress"] = progress;
        socket.emit(EVENT_DOWNLOAD_OTA, output);
        ESP_LOGI("Download OTA", "HTTP update process at %d of %d bytes... (%d %%)", currentBytes, totalBytes,
                 progress);
    }
    previousProgress = progress;
}

void update_finished() {
    ESP_LOGI(TAG, "Update finished");
    doc["status"] = "finished";
    serializeJson(doc, output);
    socket.emit(EVENT_DOWNLOAD_OTA, output);

    // delay to allow the event to be sent out
    vTaskDelay(100 / portTICK_PERIOD_MS);
}

void updateTask(void *param) {
    WiFiClientSecure client;
    client.setCACertBundle(rootca_crt_bundle_start);
    client.setTimeout(10);

    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    httpUpdate.rebootOnUpdate(true);

    String url = *((String *)param);
    httpUpdate.onStart(update_started);
    httpUpdate.onProgress(update_progress);
    httpUpdate.onEnd(update_finished);

    t_httpUpdate_return ret = httpUpdate.update(client, url.c_str());
    JsonObject jsonObject;

    switch (ret) {
        case HTTP_UPDATE_FAILED:
            doc["status"] = "error";
            doc["error"] = httpUpdate.getLastErrorString().c_str();
            serializeJson(doc, output);
            socket.emit(EVENT_DOWNLOAD_OTA, output);
            ESP_LOGE(TAG, "HTTP Update failed with error (%d): %s", httpUpdate.getLastError(),
                     httpUpdate.getLastErrorString().c_str());
            break;

        case HTTP_UPDATE_NO_UPDATES:
            doc["status"] = "error";
            doc["error"] = "Update failed, has same firmware version";
            serializeJson(doc, output);
            socket.emit(EVENT_DOWNLOAD_OTA, output);

            ESP_LOGE(TAG, "HTTP Update failed, has same firmware version");
            break;
        case HTTP_UPDATE_OK: ESP_LOGI(TAG, "HTTP Update successful - Restarting"); break;
    }
    vTaskDelete(NULL);
}

DownloadFirmwareService::DownloadFirmwareService() {}

esp_err_t DownloadFirmwareService::handleDownloadUpdate(PsychicRequest *request, JsonVariant &json) {
    if (!json.is<JsonObject>()) {
        return request->reply(400);
    }

    String downloadURL = json["download_url"];
    ESP_LOGI(TAG, "Starting OTA from: %s", downloadURL.c_str());

    doc["status"] = "preparing";
    doc["progress"] = 0;
    doc["error"] = "";

    serializeJson(doc, output);

    socket.emit(EVENT_DOWNLOAD_OTA, output);

    if (xTaskCreatePinnedToCore(&updateTask, "Update", OTA_TASK_STACK_SIZE, &downloadURL, (configMAX_PRIORITIES - 1),
                                NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "Couldn't create download OTA task");
        return request->reply(500);
    }
    return request->reply(200);
}