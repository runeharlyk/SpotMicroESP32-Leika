#pragma once

#include <esp_http_server.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <string>

#define ESP_FS LittleFS

#define AP_SETTINGS_FILE "/config/apSettings.json"
#define CAMERA_SETTINGS_FILE "/config/cameraSettings.json"
#define FS_CONFIG_DIRECTORY "/config"
#define DEVICE_CONFIG_FILE "/config/peripheral.json"
#define WIFI_SETTINGS_FILE "/config/wifiSettings.json"
#define SERVO_SETTINGS_FILE "/config/servoSettings.json"
#define MDNS_SETTINGS_FILE "/config/mdnsSettings.json"

namespace FileSystem {

std::string listFiles(const std::string &directory, bool isRoot = true);
bool deleteFile(const char *filename);
bool editFile(const char *filename, const char *content);

esp_err_t getFiles(httpd_req_t *request);
esp_err_t getConfigFile(httpd_req_t *request);
esp_err_t handleDelete(httpd_req_t *request, JsonVariant &json);
esp_err_t handleEdit(httpd_req_t *request, JsonVariant &json);
esp_err_t mkdir(httpd_req_t *request, JsonVariant &json);

} // namespace FileSystem
