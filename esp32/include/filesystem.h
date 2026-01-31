#pragma once

#include <esp_http_server.h>
#include <esp_littlefs.h>
#include <esp_vfs.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <cstdio>
#include <platform_shared/api.pb.h>

#define MOUNT_POINT "/littlefs"

#define FS_CONFIG_DIRECTORY MOUNT_POINT "/config"
#define DEVICE_CONFIG_FILE MOUNT_POINT "/config/peripheral.pb"
#define CAMERA_SETTINGS_FILE MOUNT_POINT "/config/cameraSettings.pb"
#define AP_SETTINGS_FILE MOUNT_POINT "/config/apSettings.pb"
#define MDNS_SETTINGS_FILE MOUNT_POINT "/config/mdnsSettings.pb"
#define WIFI_SETTINGS_FILE MOUNT_POINT "/config/wifiSettings.pb"
#define PERIPHERAL_SETTINGS_FILE MOUNT_POINT "/config/peripheralSettings.pb"
#define SERVO_SETTINGS_FILE MOUNT_POINT "/config/servoSettings.pb"

namespace FileSystem {

bool init();

void listFilesProto(const std::string &directory, api_FileEntry *entry);
std::string listFiles(const std::string &directory, bool isRoot = true);
bool deleteFile(const char *filename);
bool editFile(const char *filename, const uint8_t *content, size_t size);
bool editFile(const char *filename, const char *content);
bool fileExists(const char *filename);
std::string readFile(const char *filename);
bool writeFile(const char *filename, const char *content);
bool writeFile(const char *filename, const uint8_t *content, size_t size);
bool mkdirRecursive(const char *path);

esp_err_t getFilesProto(httpd_req_t *request);
esp_err_t getFiles(httpd_req_t *request);
esp_err_t getConfigFile(httpd_req_t *request);
esp_err_t handleDelete(httpd_req_t *request, const api_FileDeleteRequest &req);
esp_err_t handleEdit(httpd_req_t *request, const api_FileEditRequest &req);
esp_err_t mkdir(httpd_req_t *request, const api_FileMkdirRequest &req);

} // namespace FileSystem
