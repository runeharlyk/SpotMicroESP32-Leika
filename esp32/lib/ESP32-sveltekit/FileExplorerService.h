#ifndef FileExplorer_h
#define FileExplorer_h

#include <ESPFS.h>
#include <PsychicHttp.h>

#define FILE_EXPLORER_SERVICE_PATH "/api/files"
#define FILE_EXPLORER_DELETE_SERVICE_PATH "/api/files/delete"

class FileExplorer {
  public:
    FileExplorer(PsychicHttpServer *server) : _server(server) {}

    void begin() {
        _server->on(FILE_EXPLORER_SERVICE_PATH, HTTP_GET, [this](PsychicRequest *request) { return explore(request); });
        _server->on(FILE_EXPLORER_DELETE_SERVICE_PATH, HTTP_POST,
                    [this](PsychicRequest *request, JsonVariant &json) { return deleteFile(request, json); });

        ESP_LOGV("APStatus", "Registered GET endpoint: %s", FILE_EXPLORER_SERVICE_PATH);
    }

  private:
    PsychicHttpServer *_server;

    esp_err_t explore(PsychicRequest *request) {
        return request->reply(200, "application/json", listFiles("/").c_str());
    }

    esp_err_t deleteFile(PsychicRequest *request, JsonVariant &json) {
        if (json.is<JsonObject>()) {
            String filename = json["file"];
            ESP_LOGI("FileExplorer", "Deleting file: %s", filename.c_str());
            return ESPFS.remove(filename.c_str()) ? request->reply(200) : request->reply(500);
        }
        return request->reply(400);
    }

    String listFiles(const String &directory, bool isRoot = true) {
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
};

#endif // end FileExplorer_h
