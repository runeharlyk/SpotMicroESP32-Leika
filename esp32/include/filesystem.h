#pragma once

#include <esp_http_server.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <string>
#include <platform_shared/api.pb.h>

#define ESP_FS LittleFS

#define FS_CONFIG_DIRECTORY "/config"
#define DEVICE_CONFIG_FILE "/config/peripheral.json"'
#define CAMERA_SETTINGS_FILE "/config/cameraSettings.pb"
#define AP_SETTINGS_FILE "/config/apSettings.pb"
#define MDNS_SETTINGS_FILE "/config/mdnsSettings.pb"
#define WIFI_SETTINGS_FILE "/config/wifiSettings.pb"
#define PERIPHERAL_SETTINGS_FILE "/config/peripheralSettings.pb"
#define SERVO_SETTINGS_FILE "/config/servoSettings.pb"


namespace FileSystem {

void listFilesProto(const std::string &directory, api_FileEntry *entry);
std::string listFiles(const std::string &directory, bool isRoot = true);
bool deleteFile(const char *filename);
bool editFile(const char *filename, const uint8_t *content, size_t size);

esp_err_t getFilesProto(httpd_req_t *request);
esp_err_t getFiles(httpd_req_t *request);
esp_err_t getConfigFile(httpd_req_t *request);
esp_err_t handleDelete(httpd_req_t *request, const api_FileDeleteRequest &req);
esp_err_t handleEdit(httpd_req_t *request, const api_FileEditRequest &req);
esp_err_t mkdir(httpd_req_t *request, const api_FileMkdirRequest &req);

} // namespace FileSystem
