#include <filesystem.h>
#include <communication/native_server.h>

static const char *TAG = "FileService";

namespace FileSystem {

esp_err_t getFiles(httpd_req_t *request) {
    std::string files = listFiles("/");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, files.c_str(), files.length());
}

esp_err_t getConfigFile(httpd_req_t *request) {
    const char *uri = request->uri;
    std::string path = "/config" + std::string(uri).substr(11);
    if (!ESP_FS.exists(path.c_str())) {
        return NativeServer::sendError(request, 404, "File not found");
    }
    File file = ESP_FS.open(path.c_str(), "r");
    if (!file) {
        return NativeServer::sendError(request, 500, "Failed to open file");
    }
    String content = file.readString();
    file.close();
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, content.c_str(), content.length());
}

esp_err_t handleDelete(httpd_req_t *request, JsonVariant &json) {
    if (json.is<JsonObject>()) {
        const char *filename = json["file"].as<const char *>();
        ESP_LOGI(TAG, "Deleting file: %s", filename);
        return deleteFile(filename) ? NativeServer::sendOk(request)
                                    : NativeServer::sendError(request, 500, "Delete failed");
    }
    return NativeServer::sendError(request, 400, "Invalid request");
}

esp_err_t handleEdit(httpd_req_t *request, JsonVariant &json) {
    if (json.is<JsonObject>()) {
        const char *filename = json["file"].as<const char *>();
        const char *content = json["content"].as<const char *>();
        ESP_LOGI(TAG, "Editing file: %s", filename);
        return editFile(filename, content) ? NativeServer::sendOk(request)
                                           : NativeServer::sendError(request, 500, "Edit failed");
    }
    return NativeServer::sendError(request, 400, "Invalid request");
}

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

bool editFile(const char *filename, const char *content) {
    File file = ESP_FS.open(filename, FILE_WRITE);
    if (!file) return false;

    file.print(content);
    file.close();
    return true;
}

esp_err_t mkdir(httpd_req_t *request, JsonVariant &json) {
    const char *path = json["path"].as<const char *>();
    ESP_LOGI(TAG, "Creating directory: %s", path);
    return ESP_FS.mkdir(path) ? NativeServer::sendOk(request) : NativeServer::sendError(request, 500, "mkdir failed");
}

} // namespace FileSystem
