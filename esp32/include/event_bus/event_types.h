#pragma once
#include <cstdint>

enum class EventType : uint16_t {
    WIFI_SETTINGS = 100,
    AP_SETTINGS = 110,
    MDNS_SETTINGS = 120,
    PERIPHERAL_SETTINGS = 130,
    SERVO_SETTINGS = 140,
    CAMERA_SETTINGS = 150,

    WIFI_STATUS = 101,
    AP_STATUS = 111,
    IMU_DATA = 131,
    MOTION_COMMAND = 200,
    MOTION_MODE = 201,
    SERVO_STATE = 141,

    SYSTEM_BOOT = 300,
    STORAGE_HYDRATION_COMPLETE = 301,
};

const char* eventTypeName(EventType type);
bool isSettingsEvent(EventType type);
