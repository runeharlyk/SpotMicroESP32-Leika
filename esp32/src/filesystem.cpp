#include <filesystem.h>

static const char* TAG = "FileService";

namespace FileSystem {

void begin() { ESP_FS.begin(true); }

esp_err_t getFiles(HttpRequest& request) {
    std::string json = listFilesJson("/");
    return request.reply(200, "application/json", (const uint8_t*)json.c_str(), json.length());
}

esp_err_t getConfigFile(HttpRequest& request) {
    std::string uri = request.uri();
    std::string path = "/config" + uri.substr(11);

    if (!ESP_FS.exists(path.c_str())) {
        return request.reply(404);
    }

    File file = ESP_FS.open(path.c_str(), "r");
    if (!file) {
        return request.reply(500);
    }

    size_t fileSize = file.size();
    uint8_t* buffer = (uint8_t*)malloc(fileSize);
    if (!buffer) {
        file.close();
        return request.reply(500);
    }

    file.read(buffer, fileSize);
    file.close();

    esp_err_t ret = request.reply(200, "application/x-protobuf", buffer, fileSize);
    free(buffer);
    return ret;
}

esp_err_t handleDelete(HttpRequest& request) {
    socket_message_FileDeleteRequest proto = socket_message_FileDeleteRequest_init_zero;
    if (!request.decodeProto(proto, socket_message_FileDeleteRequest_fields)) {
        return request.reply(400);
    }

    ESP_LOGI(TAG, "Deleting file: %s", proto.file);
    return deleteFile(proto.file) ? request.reply(200) : request.reply(500);
}

esp_err_t handleEdit(HttpRequest& request) {
    socket_message_FileEditRequest proto = socket_message_FileEditRequest_init_zero;
    if (!request.decodeProto(proto, socket_message_FileEditRequest_fields)) {
        return request.reply(400);
    }

    ESP_LOGI(TAG, "Editing file: %s", proto.file);
    return editFile(proto.file, proto.content) ? request.reply(200) : request.reply(500);
}

bool deleteFile(const char* filename) { return ESP_FS.remove(filename); }

std::string listFilesJson(const std::string& directory, bool isRoot) {
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
            output += "\"" + name + "\": " + listFilesJson(name, false);
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

esp_err_t handleUpload(HttpRequest& request) { return request.reply(501); }

bool editFile(const char* filename, const char* content) {
    File file = ESP_FS.open(filename, FILE_WRITE);
    if (!file) return false;

    file.print(content);
    file.close();
    return true;
}

esp_err_t mkdir(HttpRequest& request) {
    socket_message_FileMkdirRequest proto = socket_message_FileMkdirRequest_init_zero;
    if (!request.decodeProto(proto, socket_message_FileMkdirRequest_fields)) {
        return request.reply(400);
    }

    ESP_LOGI(TAG, "Creating directory: %s", proto.path);
    return ESP_FS.mkdir(proto.path) ? request.reply(200) : request.reply(500);
}

} // namespace FileSystem
