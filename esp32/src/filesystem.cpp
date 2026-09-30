#include <filesystem.h>
#include <cstring>
#include <esp_log.h>

static const char *TAG = "FileSystem";

namespace FileSystem {

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

} // namespace FileSystem
