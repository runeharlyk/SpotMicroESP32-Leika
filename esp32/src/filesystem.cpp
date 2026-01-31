#include <filesystem.h>
#include <communication/webserver.h>
#include <vector>
#include <cstring>
#include "utils/string_utils.hpp"
#include <esp_log.h>

static const char *TAG = "FileSystem";

namespace FileSystem {

// Storage for dynamically allocated FileEntry arrays
static std::vector<api_FileEntry*> allocatedEntries;

static void freeAllocatedEntries() {
    for (auto ptr : allocatedEntries) {
        delete[] ptr;
    }
    allocatedEntries.clear();
}

void listFilesProto(const std::string &directory, api_FileEntry *entry) {
    File root = ESP_FS.open(directory.find("/") == 0 ? directory.c_str() : ("/" + directory).c_str());
    if (!root.isDirectory()) {
        entry->children_count = 0;
        entry->children = nullptr;
        return;
    }

    // First pass: count children
    std::vector<File> files;
    File file = root.openNextFile();
    while (file) {
        files.push_back(file);
        file = root.openNextFile();
    }

    if (files.empty()) {
        entry->children_count = 0;
        entry->children = nullptr;
        return;
    }

    // Allocate children array
    entry->children_count = files.size();
    entry->children = new api_FileEntry[files.size()];
    allocatedEntries.push_back(entry->children);

    // Fill children
    for (size_t i = 0; i < files.size(); i++) {
        api_FileEntry &child = entry->children[i];
        memset(&child, 0, sizeof(child));

        std::string name = std::string(files[i].name());
        strncpy(child.name, name.c_str(), sizeof(child.name) - 1);
        child.name[sizeof(child.name) - 1] = '\0';

        child.is_directory = files[i].isDirectory();
        if (child.is_directory) {
            listFilesProto(name, &child);
        } else {
            child.size = files[i].size();
            child.children_count = 0;
            child.children = nullptr;
        }
    }
}

esp_err_t getFilesProto(httpd_req_t *request) {
    freeAllocatedEntries();  // Clean up any previous allocations

    api_Response res = api_Response_init_zero;
    res.status_code = 200;
    res.which_payload = api_Response_file_list_tag;

    // Create root entry
    api_FileEntry rootEntry = api_FileEntry_init_zero;
    strncpy(rootEntry.name, "root", sizeof(rootEntry.name) - 1);
    rootEntry.is_directory = true;
    listFilesProto("/", &rootEntry);

    // Allocate entries array for FileList
    res.payload.file_list.entries_count = 1;
    res.payload.file_list.entries = new api_FileEntry[1];
    allocatedEntries.push_back(res.payload.file_list.entries);
    res.payload.file_list.entries[0] = rootEntry;

    esp_err_t result = WebServer::send(request, 200, res, api_Response_fields);

    freeAllocatedEntries();  // Clean up after sending
    return result;
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

esp_err_t getFiles(httpd_req_t *request) {
    std::string files = listFiles(MOUNT_POINT);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, files.c_str(), files.length());
}

esp_err_t getConfigFile(httpd_req_t *request) {
    const char *uri = request->uri;
    std::string path = std::string(MOUNT_POINT) + "/config" + std::string(uri).substr(11);

    if (!fileExists(path.c_str())) {
        return WebServer::sendError(request, 404, "File not found");
    }

    std::string content = readFile(path.c_str());
    if (content.empty()) {
        return WebServer::sendError(request, 500, "Failed to read file");
    }
    String content = file.readString();
    file.close();
    if (ends_with(path, ".pb")) {
        httpd_resp_set_type(request, "application/x-protobuf");
    } else if (ends_with(path, ".json")) {
        httpd_resp_set_type(request, "application/json");
    } else {
        httpd_resp_set_type(request, "text/plain");
    }
    return httpd_resp_send(request, content.c_str(), content.length());
}

esp_err_t handleDelete(httpd_req_t *request, const api_FileDeleteRequest &req) {
    ESP_LOGI(TAG, "Deleting file: %s", req.path);

    api_Response res = api_Response_init_zero;
    if (deleteFile(req.path)) {
        res.status_code = 200;
        res.which_payload = api_Response_empty_message_tag;
        return WebServer::send(request, 200, res, api_Response_fields);
    } else {
        return WebServer::sendError(request, 500, "Delete failed");

    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, content.c_str(), content.length());
}

esp_err_t handleDelete(httpd_req_t *request, JsonVariant &json) {
    if (json.is<JsonObject>()) {
        const char *filename = json["file"].as<const char *>();
        std::string fullPath = std::string(MOUNT_POINT) + filename;
        ESP_LOGI(TAG, "Deleting file: %s", fullPath.c_str());
        return deleteFile(fullPath.c_str()) ? WebServer::sendOk(request)
                                            : WebServer::sendError(request, 500, "Delete failed");
    }
}

esp_err_t handleEdit(httpd_req_t *request, const api_FileEditRequest &req) {
    ESP_LOGI(TAG, "Editing file: %s", req.path);

    api_Response res = api_Response_init_zero;
    if (editFile(req.path, req.content->bytes, req.content->size)) {
        res.status_code = 200;
        res.which_payload = api_Response_empty_message_tag;
        return WebServer::send(request, 200, res, api_Response_fields);
    } else {
        return WebServer::sendError(request, 500, "Edit failed");
esp_err_t handleEdit(httpd_req_t *request, JsonVariant &json) {
    if (json.is<JsonObject>()) {
        const char *filename = json["file"].as<const char *>();
        const char *content = json["content"].as<const char *>();
        std::string fullPath = std::string(MOUNT_POINT) + filename;
        ESP_LOGI(TAG, "Editing file: %s", fullPath.c_str());
        return editFile(fullPath.c_str(), content) ? WebServer::sendOk(request)
                                                   : WebServer::sendError(request, 500, "Edit failed");
    }
}

bool deleteFile(const char *filename) { return remove(filename) == 0; }

std::string listFiles(const std::string &directory, bool isRoot) {
    DIR *dir = opendir(directory.c_str());
    if (!dir) {
        return isRoot ? "{ \"root\": {} }" : "{}";
    }

    std::string output = isRoot ? "{ \"root\": {" : "{";
    bool first = true;

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (!first) {
            output += ", ";
        }
        first = false;

        std::string fullPath = directory + "/" + entry->d_name;
        struct stat st;
        if (stat(fullPath.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                output += "\"" + std::string(entry->d_name) + "\": " + listFiles(fullPath, false);
            } else {
                output += "\"" + std::string(entry->d_name) + "\": " + std::to_string(st.st_size);
            }
        }
    }

    closedir(dir);

    output += "}";
    if (isRoot) output += "}";

    return output;
}

bool editFile(const char *filename, const uint8_t *content, size_t size) {
    File file = ESP_FS.open(filename, FILE_WRITE);
    if (!file) return false;

    file.write(content, size);
    file.close();
    return true;
}

esp_err_t mkdir(httpd_req_t *request, const api_FileMkdirRequest &req) {
    ESP_LOGI(TAG, "Creating directory: %s", req.path);

    api_Response res = api_Response_init_zero;
    if (ESP_FS.mkdir(req.path)) {
        res.status_code = 200;
        res.which_payload = api_Response_empty_message_tag;
        return WebServer::send(request, 200, res, api_Response_fields);
    } else {
        return WebServer::sendError(request, 500, "mkdir failed");
    }
bool editFile(const char *filename, const char *content) { return writeFile(filename, content); }

esp_err_t mkdir(httpd_req_t *request, JsonVariant &json) {
    const char *path = json["path"].as<const char *>();
    std::string fullPath = std::string(MOUNT_POINT) + path;
    ESP_LOGI(TAG, "Creating directory: %s", fullPath.c_str());
    return mkdirRecursive(fullPath.c_str()) ? WebServer::sendOk(request)
                                            : WebServer::sendError(request, 500, "mkdir failed");
}

} // namespace FileSystem
