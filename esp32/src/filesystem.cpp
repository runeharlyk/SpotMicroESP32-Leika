#include <filesystem.h>
#include <cstring>
#include <esp_log.h>
#ifdef SD_CMD_PIN
#include <esp_vfs_fat.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>
#endif

static const char *TAG = "FileSystem";

namespace FileSystem {

#ifdef SD_CMD_PIN
// A card in a 1-bit SDMMC slot, mounted beside LittleFS and never needed: a board without a card boots as before.
static void mountSdCard() {
    const esp_vfs_fat_sdmmc_mount_config_t mountConfig = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = static_cast<gpio_num_t>(SD_CLK_PIN);
    slot.cmd = static_cast<gpio_num_t>(SD_CMD_PIN);
    slot.d0 = static_cast<gpio_num_t>(SD_D0_PIN);
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    sdmmc_card_t *card = nullptr;
    const esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mountConfig, &card);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No SD card mounted (%s)", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "SD card %s, %llu MB, mounted at %s", card->cid.name,
             static_cast<unsigned long long>(card->csd.capacity) * card->csd.sector_size / (1024 * 1024),
             SD_MOUNT_POINT);
}
#endif

bool init() {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = MOUNT_POINT,
        .partition_label = "spiffs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return false;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info("spiffs", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    mkdirRecursive(FS_CONFIG_DIRECTORY);
    mkdirRecursive(FS_ANIMATION_DIRECTORY);
#ifdef SD_CMD_PIN
    mountSdCard();
#endif

    return true;
}

bool fileExists(const char *filename) {
    struct stat st;
    return stat(filename, &st) == 0;
}

std::string readFile(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        return "";
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string content;
    content.resize(size);
    fread(&content[0], 1, size, f);
    fclose(f);

    return content;
}

bool writeFile(const char *filename, const char *content) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filename);
        return false;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    return written == len;
}

bool writeFile(const char *filename, const uint8_t *content, size_t size) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filename);
        return false;
    }

    size_t written = fwrite(content, 1, size, f);
    fclose(f);

    return written == size;
}

bool mkdirRecursive(const char *path) {
    char tmp[256];
    char *p = nullptr;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            struct stat st;
            if (stat(tmp, &st) != 0) {
                if (::mkdir(tmp, 0755) != 0) {
                    ESP_LOGE(TAG, "Failed to create directory: %s", tmp);
                    return false;
                }
            }
            *p = '/';
        }
    }

    struct stat st;
    if (stat(tmp, &st) != 0) {
        if (::mkdir(tmp, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create directory: %s", tmp);
            return false;
        }
    }

    return true;
}

// LittleFS commits a rename over an existing file as one metadata update. Removing `to` first when this fails would
// trade a refused write for lost settings.
bool replaceFile(const char *from, const char *to) { return rename(from, to) == 0; }

} // namespace FileSystem
