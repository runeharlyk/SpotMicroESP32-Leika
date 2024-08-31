#include <services/filesystem_service.h>

static const char *TAG = "FileService";

FileSystemService::FileSystemService() {
    _uploadHandler.onUpload(uploadFile);

    _uploadHandler.onRequest([](PsychicRequest *request) { return request->reply(200); });
}

FileSystemService::~FileSystemService() {}

esp_err_t FileSystemService::getFiles(PsychicRequest *request) {
    return request->reply(200, "application/json", listFiles("/").c_str());
}

esp_err_t FileSystemService::handleDelete(PsychicRequest *request, JsonVariant &json) {
    if (json.is<JsonObject>()) {
        const char *filename = json["file"].as<const char *>();
        ESP_LOGI("FileExplorer", "Deleting file: %s", filename);
        return deleteFile(filename) ? request->reply(200) : request->reply(500);
    }
    return request->reply(400);
}

esp_err_t FileSystemService::handleEdit(PsychicRequest *request, JsonVariant &json) {
    if (json.is<JsonObject>()) {
        const char *filename = json["file"].as<const char *>();
        const char *content = json["content"].as<const char *>();

        ESP_LOGI("FileExplorer", "Editing file: %s", filename);

        return editFile(filename, content) ? request->reply(200) : request->reply(500);
    }
    return request->reply(400);
}

PsychicUploadHandler *FileSystemService::getUploadHandler() { return &_uploadHandler; }

/* Helpers */

bool FileSystemService::deleteFile(const char *filename) { return ESPFS.remove(filename); }

String FileSystemService::listFiles(const String &directory, bool isRoot) {
    File root = ESPFS.open(directory.startsWith("/") ? directory : "/" + directory);
    if (!root.isDirectory()) {
        return "";
    }

    File file = root.openNextFile();
    String output = isRoot ? "{ \"root\": {" : "{";

    while (file) {
        if (file.isDirectory()) {
            output += "\"" + String(file.name()) + "\": " + listFiles(file.name(), false) + ", ";
        } else {
            output += "\"" + String(file.name()) + "\": " + String(file.size()) + ", ";
        }
        file = root.openNextFile();
    }

    if (output.endsWith(", ")) {
        output.remove(output.length() - 2);
    }
    output += "}";
    if (isRoot) {
        output += "}";
    }
    return output;
}

esp_err_t FileSystemService::uploadFile(PsychicRequest *request, const String &filename, uint64_t index, uint8_t *data,
                                        size_t len, bool last) {
    File file;
    String path = "/www/" + filename;

    ESP_LOGI(TAG, "Writing %d/%d bytes to: %s\n", (int)index + (int)len, request->contentLength(), path.c_str());

    if (last) ESP_LOGI(TAG, "%s is finished. Total bytes: %d\n", path.c_str(), (int)index + (int)len);

    // our first call?
    file = LittleFS.open(path, !index ? FILE_WRITE : FILE_APPEND);

    if (!file) {
        ESP_LOGE(TAG, "Failed to open file");
        return ESP_FAIL;
    }

    if (!file.write(data, len)) {
        ESP_LOGE(TAG, "Write failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool FileSystemService::editFile(const char *filename, const char *content) {
    File file = ESPFS.open(filename, FILE_WRITE);
    if (!file) {
        return false;
    }

    file.print(content);
    file.close();
    return true;
}