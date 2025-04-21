#include "system_service.h"

namespace system_service {

static const char *TAG = "SystemService";

static JsonDocument analyticsDoc;
static char analyticsMessage[MAX_ESP_ANALYTICS_SIZE];

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
    root["cpu_reset_reason"] = resetReason(esp_reset_reason());
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
    root["cpu0_usage"] = g_taskManager.getCpuUsage(0);
    root["cpu1_usage"] = g_taskManager.getCpuUsage(1);
    root["cpu_usage"] = g_taskManager.getCpuUsage();
    JsonArray tasks = root["tasks"].to<JsonArray>();
    for (auto const &task : g_taskManager.getTasks()) {
        JsonObject nested = tasks.add<JsonObject>();
        nested["name"] = task.name;
        nested["stackSize"] = task.stackSize;
        nested["priority"] = task.priority;
        nested["coreId"] = task.coreId;
    }
}

void emitMetrics() {
    if (!socket.hasSubscribers(EVENT_ANALYTICS)) return;
    analyticsDoc.clear();
    JsonObject root = analyticsDoc.to<JsonObject>();
    system_service::metrics(root);
    serializeJson(analyticsDoc, analyticsMessage);
    socket.emit(EVENT_ANALYTICS, analyticsMessage);
}

const char *resetReason(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN: return "Reset reason can not be determined";
        case ESP_RST_POWERON: return "Reset due to power-on event";
        case ESP_RST_EXT: return "Reset by external pin (not applicable for ESP32)";
        case ESP_RST_SW: return "Software reset via esp_restart";
        case ESP_RST_PANIC: return "Software reset due to exception/panic";
        case ESP_RST_INT_WDT: return "Reset (software or hardware) due to interrupt watchdog";
        case ESP_RST_TASK_WDT: return "Reset due to task watchdog";
        case ESP_RST_WDT: return "Reset due to other watchdogs";
        case ESP_RST_DEEPSLEEP: return "Reset after exiting deep sleep mode";
        case ESP_RST_BROWNOUT: return "Brownout reset (software or hardware)";
        case ESP_RST_SDIO: return "Reset over SDIO";
#ifdef ESP_RST_USB
        case ESP_RST_USB: return "Reset by USB peripheral";
#endif
#ifdef ESP_RST_JTAG
        case ESP_RST_JTAG: return "Reset by JTAG";
#endif
#ifdef ESP_RST_EFUSE
        case ESP_RST_EFUSE: return "Reset due to efuse error";
#endif
#ifdef ESP_RST_PWR_GLITCH
        case ESP_RST_PWR_GLITCH: return "Reset due to power glitch detected";
#endif
#ifdef ESP_RST_CPU_LOCKUP
        case ESP_RST_CPU_LOCKUP: return "Reset due to CPU lock up (double exception)";
#endif
        default:
            char buffer[50];
            snprintf(buffer, sizeof(buffer), "Unknown reset reason (%d)", reason);
            return buffer;
    }
}

} // namespace system_service