#pragma once

#include <communication/http_server.h>
#include <platform_shared/message.pb.h>
#include <FS.h>
#include <LittleFS.h>

#define ESP_FS LittleFS
#define FS_CONFIG_DIRECTORY "/config"

#define AP_SETTINGS_FILE "/config/apSettings.pb"
#define CAMERA_SETTINGS_FILE "/config/cameraSettings.pb"
#define DEVICE_CONFIG_FILE "/config/peripheral.pb"
#define WIFI_SETTINGS_FILE "/config/wifiSettings.pb"
#define SERVO_SETTINGS_FILE "/config/servoSettings.pb"
#define MDNS_SETTINGS_FILE "/config/mdnsSettings.pb"

namespace FileSystem {

void begin();

bool deleteFile(const char* filename);
bool editFile(const char* filename, const char* content);
std::string listFilesJson(const std::string& directory, bool isRoot = true);

esp_err_t getFiles(HttpRequest& request);
esp_err_t getConfigFile(HttpRequest& request);
esp_err_t handleDelete(HttpRequest& request);
esp_err_t handleEdit(HttpRequest& request);
esp_err_t handleUpload(HttpRequest& request);
esp_err_t mkdir(HttpRequest& request);

} // namespace FileSystem
