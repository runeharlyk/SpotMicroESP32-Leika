#include "system_service.h"

namespace system_service {

static const char *TAG = "SystemService";

esp_err_t handleReset(PsychicRequest *request) {
    reset();
    return request->reply(200);
}

esp_err_t handleRestart(PsychicRequest *request) {
    restart();
    return request->reply(200);
}

esp_err_t handleSleep(PsychicRequest *request) {
    sleep();
    return request->reply(200);
}

esp_err_t getStatus(PsychicRequest *request) {
    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject root = response.getRoot();
    status(root);
    return response.send();
}

esp_err_t getMetrics(PsychicRequest *request) {
    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject root = response.getRoot();
    metrics(root);
    return response.send();
}

void reset() {
    ESP_LOGI(TAG, "Resetting device");
    File root = ESPFS.open(FS_CONFIG_DIRECTORY);
    File file;
    while (file = root.openNextFile()) {
        String path = file.path();
        file.close();
        ESPFS.remove(path);
    }
    restart();
}

void restart() {
    xTaskCreate(
        [](void *pvParameters) {
            for (;;) {
                delay(250);
                MDNS.end();
                delay(100);
                WiFi.disconnect(true);
                delay(500);
                ESP.restart();
            }
        },
        "Restart task", 4096, nullptr, 10, nullptr);
}

void sleep() {
    xTaskCreate(
        [](void *pvParameters) {
            for (;;) {
                delay(250);
                MDNS.end();
                delay(100);
                WiFi.disconnect(true);
                delay(500);

                uint64_t bitmask = (uint64_t)1 << (WAKEUP_PIN_NUMBER);

#ifdef CONFIG_IDF_TARGET_ESP32C3
                esp_deep_sleep_enable_gpio_wakeup(bitmask, (esp_deepsleep_gpio_wake_up_mode_t)WAKEUP_SIGNAL);
#else
                esp_sleep_enable_ext1_wakeup(bitmask, (esp_sleep_ext1_wakeup_mode_t)WAKEUP_SIGNAL);
                esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
#endif
                esp_deep_sleep_start();
            }
        },
        "Sleep task", 4096, nullptr, 10, nullptr);
    ESP_LOGI(TAG, "Setting device to sleep");
}

void status(JsonObject &root) {
    root["esp_platform"] = ESP_PLATFORM;
    root["firmware_version"] = APP_VERSION;
    root["max_alloc_heap"] = ESP.getMaxAllocHeap();
    root["psram_size"] = ESP.getPsramSize();
    root["free_psram"] = ESP.getFreePsram();
    root["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
    root["cpu_type"] = ESP.getChipModel();
    root["cpu_rev"] = ESP.getChipRevision();
    root["cpu_cores"] = ESP.getChipCores();
    root["free_heap"] = ESP.getFreeHeap();
    root["min_free_heap"] = ESP.getMinFreeHeap();
    root["sketch_size"] = ESP.getSketchSize();
    root["free_sketch_space"] = ESP.getFreeSketchSpace();
    root["sdk_version"] = ESP.getSdkVersion();
    root["arduino_version"] = ARDUINO_VERSION;
    root["flash_chip_size"] = ESP.getFlashChipSize();
    root["flash_chip_speed"] = ESP.getFlashChipSpeed();
    root["fs_total"] = ESPFS.totalBytes();
    root["fs_used"] = ESPFS.usedBytes();
    root["core_temp"] = temperatureRead();
    root["cpu_reset_reason"] = resetReason(rtc_get_reset_reason(0));
    root["uptime"] = millis() / 1000;
}

void metrics(JsonObject &root) {
    root["uptime"] = millis() / 1000;
    root["free_heap"] = ESP.getFreeHeap();
    root["total_heap"] = ESP.getHeapSize();
    root["min_free_heap"] = ESP.getMinFreeHeap();
    root["max_alloc_heap"] = ESP.getMaxAllocHeap();
    root["fs_used"] = ESPFS.usedBytes();
    root["fs_total"] = ESPFS.totalBytes();
    root["core_temp"] = temperatureRead();
}

const char *resetReason(int reason) {
    switch (reason) {
        case 1: return "Vbat power on reset";
        case 3: return "Software reset digital core";
        case 4: return "Legacy watch dog reset digital core";
        case 5: return "Deep Sleep reset digital core";
        case 6: return "Reset by SLC module, reset digital core";
        case 7: return "Timer Group0 Watch dog reset digital core";
        case 8: return "Timer Group1 Watch dog reset digital core";
        case 9: return "RTC Watch dog Reset digital core";
        case 10: return "Intrusion tested to reset CPU";
        case 11: return "Time Group reset CPU";
        case 12: return "Software reset CPU";
        case 13: return "RTC Watch dog Reset CPU";
        case 14: return "for APP CPU, reset by PRO CPU";
        case 15: return "Reset when the vdd voltage is not stable";
        case 16: return "RTC Watch dog reset digital core and rtc module";
        default: return "NO_MEAN";
    }
}

} // namespace system_service