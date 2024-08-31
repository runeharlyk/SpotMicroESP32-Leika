#pragma once

#include <PsychicHttp.h>
#include <ESPFS.h>

class FileSystemService {
  private:
    static String listFiles(const String &directory, bool isRoot = true);
    static bool deleteFile(const char *filename);
    static bool editFile(const char *filename, const char *content);
    static esp_err_t uploadFile(PsychicRequest *request, const String &filename, uint64_t index, uint8_t *data,
                                size_t len, bool last);

    PsychicUploadHandler _uploadHandler;

  public:
    FileSystemService();
    ~FileSystemService();

    static esp_err_t getFiles(PsychicRequest *request);
    static esp_err_t handleDelete(PsychicRequest *request, JsonVariant &json);
    static esp_err_t handleEdit(PsychicRequest *request, JsonVariant &json);

    PsychicUploadHandler *getUploadHandler();
};
