#include <filesystem.h>
#include <string>

static const char *TAG = "FileService";

namespace FileSystem {

PsychicUploadHandler *uploadHandler;

class Initializer {
  public:
    Initializer() {
        uploadHandler = new PsychicUploadHandler();
        uploadHandler->onUpload([](PsychicRequest *request, const String &filename, uint64_t index, uint8_t *data,
                                   size_t len, bool last) -> esp_err_t {
            return uploadFile(request, std::string(filename.c_str()), index, data, len, last);
        });
        uploadHandler->onRequest([](PsychicRequest *request) { return request->reply(200); });
    }
};

static Initializer initializer;

esp_err_t getFiles(PsychicRequest *request) { return request->reply(200, "application/json", listFiles("/").c_str()); }

esp_err_t handleDelete(PsychicRequest *request, JsonVariant &json) {
    if (json.is<JsonObject>()) {
        const char *filename = json["file"].as<const char *>();
        ESP_LOGI(TAG, "Deleting file: %s", filename);
        return deleteFile(filename) ? request->reply(200) : request->reply(500);
    }
    return request->reply(400);
}

esp_err_t handleEdit(PsychicRequest *request, JsonVariant &json) {
    if (json.is<JsonObject>()) {
        const char *filename = json["file"].as<const char *>();
        const char *content = json["content"].as<const char *>();
        ESP_LOGI(TAG, "Editing file: %s", filename);
        return editFile(filename, content) ? request->reply(200) : request->reply(500);
    }
    return request->reply(400);
}

/* Helpers */

bool deleteFile(const char *filename) { return ESP_FS.remove(filename); }

std::string listFiles(const std::string &directory, bool isRoot) {
    File root = ESP_FS.open(directory.find("/") == 0 ? directory.c_str() : ("/" + directory).c_str());
    if (!root.isDirectory()) return "{}";

    File file = root.openNextFile();
    if (!file) {
        return isRoot ? "{ \"root\": {} }" : "{}";
    }

    std::string output = isRoot ? "{ \"root\": {" : "{";

    while (file) {
        std::string name = std::string(file.name());
        if (file.isDirectory()) {
            output += "\"" + name + "\": " + listFiles(name, false);
        } else {
            output += "\"" + name + "\": " + std::to_string(file.size());
        }

        File next = root.openNextFile();
        if (next) output += ", ";
        file = next;
    }

    output += "}";
    if (isRoot) output += "}";

    return output;
}

esp_err_t uploadFile(PsychicRequest *request, const std::string &filename, uint64_t index, uint8_t *data, size_t len,
                     bool last) {
    File file;
    std::string path = "/www/" + filename;
    ESP_LOGI(TAG, "Writing %d/%d bytes to: %s\n", (int)index + (int)len, request->contentLength(), path.c_str());

    if (last) ESP_LOGI(TAG, "%s is finished. Total bytes: %d\n", path.c_str(), (int)index + (int)len);

    file = ESP_FS.open(path.c_str(), !index ? FILE_WRITE : FILE_APPEND);
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

bool editFile(const char *filename, const char *content) {
    File file = ESP_FS.open(filename, FILE_WRITE);
    if (!file) return false;

    file.print(content);
    file.close();
    return true;
}

esp_err_t mkdir(PsychicRequest *request, JsonVariant &json) {
    const char *path = json["path"].as<const char *>();
    ESP_LOGI(TAG, "Creating directory: %s", path);
    return ESP_FS.mkdir(path) ? request->reply(200) : request->reply(500);
}

} // namespace FileSystem