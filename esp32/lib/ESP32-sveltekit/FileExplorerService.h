#ifndef FileExplorer_h
#define FileExplorer_h

#include <ESPFS.h>
#include <PsychicHttp.h>
#include <SecurityManager.h>

#define FILE_EXPLORER_SERVICE_PATH "/api/files/list"

class FileExplorer
{
  public:
    FileExplorer(PsychicHttpServer *server, SecurityManager *securityManager)
        : _server(server), _securityManager(securityManager)
    {
    }

    void begin()
    {
        _server->on(FILE_EXPLORER_SERVICE_PATH, HTTP_GET,
                    _securityManager->wrapRequest(std::bind(&FileExplorer::explore, this, std::placeholders::_1),
                                                  AuthenticationPredicates::IS_AUTHENTICATED));

        ESP_LOGV("APStatus", "Registered GET endpoint: %s", FILE_EXPLORER_SERVICE_PATH);
    }

  private:
    PsychicHttpServer *_server;
    SecurityManager *_securityManager;
    esp_err_t explore(PsychicRequest *request)
    {
        return request->reply(200, "application/json", listFiles("/").c_str());
    }

    String listFiles(const String &directory, bool isRoot = true)
    {
        File root = ESPFS.open(directory.startsWith("/") ? directory : "/" + directory);
        if (!root.isDirectory())
        {
            return "";
        }

        File file = root.openNextFile();
        String output = isRoot ? "{ \"root\": {" : "{";

        while (file)
        {
            if (file.isDirectory())
            {
                output += "\"" + String(file.name()) + "\": " + listFiles(file.name(), false) + ", ";
            }
            else
            {
                output += "\"" + String(file.name()) + "\": " + String(file.size()) + ", ";
            }
            file = root.openNextFile();
        }

        if (output.endsWith(", "))
        {
            output.remove(output.length() - 2);
        }
        output += "}";
        if (isRoot)
        {
            output += "}";
        }
        return output;
    }
};

#endif // end FileExplorer_h
