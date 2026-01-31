#include "system_service.h"
#include <communication/webserver.h>
#include <dirent.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <soc/soc.h>

#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6
#include <driver/temperature_sensor.h>

static float temperatureRead() {
    static temperature_sensor_handle_t temp_sensor = nullptr;
    static bool initialized = false;

    if (!initialized) {
        temperature_sensor_config_t temp_sensor_config = {
            .range_min = -10,
            .range_max = 80,
            .clk_src = TEMPERATURE_SENSOR_CLK_SRC_DEFAULT,
        };
        if (temperature_sensor_install(&temp_sensor_config, &temp_sensor) == ESP_OK) {
            temperature_sensor_enable(temp_sensor);
            initialized = true;
        }
    }

    if (initialized && temp_sensor) {
        float temp = 0;
        if (temperature_sensor_get_celsius(temp_sensor, &temp) == ESP_OK) {
            return temp;
        }
    }
    return 0.0f;
}
#else
static inline float temperatureRead() { return 0.0f; }
#endif

namespace system_service {

static const char *TAG = "SystemService";

esp_err_t handleReset(httpd_req_t *request) {
    reset();
    return WebServer::sendOk(request);
}

esp_err_t handleRestart(httpd_req_t *request) {
    restart();
    return WebServer::sendOk(request);
}

esp_err_t handleSleep(httpd_req_t *request) {
    sleep();
    return WebServer::sendOk(request);
}

void reset() {
    ESP_LOGI(TAG, "Resetting device");
    DIR *dir = opendir(FS_CONFIG_DIRECTORY);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            std::string path = std::string(FS_CONFIG_DIRECTORY) + "/" + entry->d_name;
            remove(path.c_str());
        }
        closedir(dir);
    }
    restart();
}

void restart() {
    xTaskCreate(
        [](void *pvParameters) {
            for (;;) {
                vTaskDelay(250 / portTICK_PERIOD_MS);
                mdns_free();
                vTaskDelay(100 / portTICK_PERIOD_MS);
                WiFi.disconnect(true);
                vTaskDelay(500 / portTICK_PERIOD_MS);
                esp_restart();
            }
        },
        "Restart task", 4096, nullptr, 10, nullptr);
}

void sleep() {
    xTaskCreate(
        [](void *pvParameters) {
            for (;;) {
                vTaskDelay(250 / portTICK_PERIOD_MS);
                mdns_free();
                vTaskDelay(100 / portTICK_PERIOD_MS);
                WiFi.disconnect(true);
                vTaskDelay(500 / portTICK_PERIOD_MS);

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

static const char *getChipModel() {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    switch (chip_info.model) {
        case CHIP_ESP32: return "ESP32";
        case CHIP_ESP32S2: return "ESP32-S2";
        case CHIP_ESP32S3: return "ESP32-S3";
        case CHIP_ESP32C3: return "ESP32-C3";
        case CHIP_ESP32C2: return "ESP32-C2";
        case CHIP_ESP32C6: return "ESP32-C6";
        case CHIP_ESP32H2: return "ESP32-H2";
        default: return "Unknown";
    }
}

static uint32_t getCpuFreqMHz() { return CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ; }

static uint8_t getChipCores() {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    return chip_info.cores;
}

static uint8_t getChipRevision() {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    return chip_info.revision;
}

static uint32_t getFlashChipSize() {
    uint32_t size = 0;
    esp_flash_get_size(NULL, &size);
    return size;
}

static uint32_t getFlashChipSpeed() {
#if defined(CONFIG_ESPTOOLPY_FLASHFREQ_120M)
    return 120000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_80M)
    return 80000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_64M)
    return 64000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_60M)
    return 60000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_48M)
    return 48000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_40M)
    return 40000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_30M)
    return 30000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_26M)
    return 26000000;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_20M)
    return 20000000;
#else
    return 80000000;
#endif
}

static uint32_t getSketchSize() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        return running->size;
    }
    return 0;
}

static uint32_t getFreeSketchSpace() {
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (next) {
        return next->size;
    }
    return 0;
}

void getStaticSystemInformation(socket_message_StaticSystemInformation &info) {
    size_t fs_total = 0, fs_used = 0;
    esp_littlefs_info("spiffs", &fs_total, &fs_used);

    info.esp_platform = (char *)ESP_PLATFORM_NAME;
    info.firmware_version = APP_VERSION;
    info.cpu_freq_mhz = getCpuFreqMHz();
    info.cpu_type = (char *)getChipModel();
    info.cpu_rev = getChipRevision();
    info.cpu_cores = getChipCores();
    info.sketch_size = getSketchSize();
    info.free_sketch_space = getFreeSketchSpace();
    info.sdk_version = (char *)esp_get_idf_version();
    info.arduino_version = "";
    info.flash_chip_size = getFlashChipSize();
    info.flash_chip_speed = getFlashChipSpeed();
    info.cpu_reset_reason = (char *)resetReason(esp_reset_reason());
}

void getAnalytics(socket_message_AnalyticsData &analytics) {
    size_t fs_total = 0, fs_used = 0;
    esp_littlefs_info("spiffs", &fs_total, &fs_used);

    analytics.max_alloc_heap = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    analytics.psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    analytics.free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    analytics.free_heap = esp_get_free_heap_size();
    analytics.total_heap = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    analytics.min_free_heap = esp_get_minimum_free_heap_size();
    analytics.core_temp = temperatureRead();
    analytics.fs_total = fs_total;
    analytics.fs_used = fs_used;
    analytics.uptime = esp_timer_get_time() / 1000;
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
            static char buffer[48];
            snprintf(buffer, sizeof(buffer), "Unknown reset reason (%d)", reason);
            return buffer;
    }
}

} // namespace system_service
