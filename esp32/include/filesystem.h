#pragma once

#include <esp_littlefs.h>
#include <esp_vfs.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <cstdio>
#include <platform_shared/api.pb.h>

#define MOUNT_POINT "/littlefs"

#define FS_CONFIG_DIRECTORY MOUNT_POINT "/config"
#define FS_ANIMATION_DIRECTORY MOUNT_POINT "/animations"
#define DEVICE_CONFIG_FILE MOUNT_POINT "/config/peripheral.pb"
#define CAMERA_SETTINGS_FILE MOUNT_POINT "/config/cameraSettings.pb"
#define AP_SETTINGS_FILE MOUNT_POINT "/config/apSettings.pb"
#define WIFI_SETTINGS_FILE MOUNT_POINT "/config/wifiSettings.pb"
#define PERIPHERAL_SETTINGS_FILE MOUNT_POINT "/config/peripheralSettings.pb"
#define SERVO_SETTINGS_FILE MOUNT_POINT "/config/servoSettings.pb"
#define ROBOT_SETTINGS_FILE MOUNT_POINT "/config/robotSettings.pb"

namespace FileSystem {

bool init();

bool fileExists(const char *filename);
std::string readFile(const char *filename);
bool writeFile(const char *filename, const char *content);
bool writeFile(const char *filename, const uint8_t *content, size_t size);
bool mkdirRecursive(const char *path);

} // namespace FileSystem
