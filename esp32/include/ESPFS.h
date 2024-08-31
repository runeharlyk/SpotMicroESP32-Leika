#ifndef ESPFS_H_
#define ESPFS_H_

#include <FS.h>
#include <LittleFS.h>

#define ESPFS LittleFS

#define FS_CONFIG_DIRECTORY "/config"

#define CAMERA_SETTINGS_FILE "/config/cameraSettings.json"
#define NTP_SETTINGS_FILE "/config/ntpSettings.json"
#define WIFI_SETTINGS_FILE "/config/wifiSettings.json"

#endif