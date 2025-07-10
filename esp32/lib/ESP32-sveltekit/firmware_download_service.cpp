/**
 *   ESP32 SvelteKit
 *
 *   A simple, secure and extensible framework for IoT projects for ESP32 platforms
 *   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
 *   https://github.com/theelims/ESP32-sveltekit
 *
 *   Copyright (C) 2023 theelims
 *
 *   All Rights Reserved. This software may be modified and distributed under
 *   the terms of the LGPL v3 license. See the LICENSE file for details.
 **/

#include <firmware_download_service.h>

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_src_certs_x509_crt_bundle_bin_start");

static int previousProgress = 0;
JsonVariant obj;

void update_started() {
    obj["status"] = "preparing";
    socket.emit(EVENT_DOWNLOAD_OTA, obj);
}

void update_progress(int currentBytes, int totalBytes) {
    obj["status"] = "progress";
    int progress = ((currentBytes * 100) / totalBytes);
    if (progress > previousProgress) {
        obj["progress"] = progress;
        socket.emit(EVENT_DOWNLOAD_OTA, obj);
        ESP_LOGV("Download OTA", "HTTP update process at %d of %d bytes... (%d %%)", currentBytes, totalBytes,
                 progress);
    }
    previousProgress = progress;
}

void update_finished() {
    obj["status"] = "finished";
    socket.emit(EVENT_DOWNLOAD_OTA, obj);

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
    // httpUpdate.onStart(update_started);
    // httpUpdate.onProgress(update_progress);
    // httpUpdate.onEnd(update_finished);

    t_httpUpdate_return ret = httpUpdate.update(client, url.c_str());

    switch (ret) {
        case HTTP_UPDATE_FAILED:
            obj["status"] = "error";
            obj["error"] = httpUpdate.getLastErrorString().c_str();
            socket.emit(EVENT_DOWNLOAD_OTA, obj);

            ESP_LOGE("Download OTA", "HTTP Update failed with error (%d): %s", httpUpdate.getLastError(),
                     httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:

            obj["status"] = "error";
            obj["error"] = "Update failed, has same firmware version";
            socket.emit(EVENT_DOWNLOAD_OTA, obj);

            ESP_LOGE("Download OTA", "HTTP Update failed, has same firmware version");
            break;
        case HTTP_UPDATE_OK: ESP_LOGI("Download OTA", "HTTP Update successful - Restarting"); break;
    }
    vTaskDelete(NULL);
}

DownloadFirmwareService::DownloadFirmwareService() {}

esp_err_t DownloadFirmwareService::handleDownloadUpdate(PsychicRequest *request, JsonVariant &json) {
    if (!json.is<JsonObject>()) {
        return request->reply(400);
    }

    String downloadURL = json["download_url"];
    ESP_LOGI("Download OTA", "Starting OTA from: %s", downloadURL.c_str());

    obj["status"] = "preparing";
    obj["progress"] = 0;
    obj["error"] = "";
    socket.emit(EVENT_DOWNLOAD_OTA, obj);

    const BaseType_t taskResult = g_taskManager.createTask(&updateTask, "Firmware download", OTA_TASK_STACK_SIZE,
                                                           &downloadURL, (configMAX_PRIORITIES - 1), NULL, 1);
    if (taskResult != pdPASS) {
        ESP_LOGE("Download OTA", "Couldn't create download OTA task");
        return request->reply(500);
    }
    return request->reply(200);
}
