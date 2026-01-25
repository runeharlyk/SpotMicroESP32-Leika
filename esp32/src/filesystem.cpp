#include <filesystem.h>
#include <communication/webserver.h>
#include <vector>
#include <cstring>

static const char *TAG = "FileService";

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

    esp_err_t result = WebServer::sendProto(request, 200, res, api_Response_fields);

    freeAllocatedEntries();  // Clean up after sending
    return result;
}

esp_err_t getFiles(httpd_req_t *request) {
    std::string files = listFiles("/");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, files.c_str(), files.length());
}

esp_err_t getConfigFile(httpd_req_t *request) {
    const char *uri = request->uri;
    std::string path = "/config" + std::string(uri).substr(11);
    if (!ESP_FS.exists(path.c_str())) {
        return WebServer::sendError(request, 404, "File not found");
    }
    File file = ESP_FS.open(path.c_str(), "r");
    if (!file) {
        return WebServer::sendError(request, 500, "Failed to open file");
    }
    String content = file.readString();
    file.close();
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, content.c_str(), content.length());
}

esp_err_t handleDelete(httpd_req_t *request, const api_FileDeleteRequest &req) {
    ESP_LOGI(TAG, "Deleting file: %s", req.path);

    api_Response res = api_Response_init_zero;
    if (deleteFile(req.path)) {
        res.status_code = 200;
        res.which_payload = api_Response_empty_message_tag;
        return WebServer::sendProto(request, 200, res, api_Response_fields);
    } else {
        return WebServer::sendError(request, 500, "Delete failed");
    }
}

esp_err_t handleEdit(httpd_req_t *request, const api_FileEditRequest &req) {
    ESP_LOGI(TAG, "Editing file: %s", req.path);

    api_Response res = api_Response_init_zero;
    if (editFile(req.path, req.content->bytes, req.content->size)) {
        res.status_code = 200;
        res.which_payload = api_Response_empty_message_tag;
        return WebServer::sendProto(request, 200, res, api_Response_fields);
    } else {
        return WebServer::sendError(request, 500, "Edit failed");
    }
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
        return WebServer::sendProto(request, 200, res, api_Response_fields);
    } else {
        return WebServer::sendError(request, 500, "mkdir failed");
    }
}

} // namespace FileSystem
