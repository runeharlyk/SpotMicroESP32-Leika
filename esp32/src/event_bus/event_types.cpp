#include <event_bus/event_types.h>

const char* eventTypeName(EventType type) {
    switch (type) {
        case EventType::WIFI_SETTINGS: return "WIFI_SETTINGS";
        case EventType::AP_SETTINGS: return "AP_SETTINGS";
        case EventType::MDNS_SETTINGS: return "MDNS_SETTINGS";
        case EventType::PERIPHERAL_SETTINGS: return "PERIPHERAL_SETTINGS";
        case EventType::SERVO_SETTINGS: return "SERVO_SETTINGS";
        case EventType::CAMERA_SETTINGS: return "CAMERA_SETTINGS";
        case EventType::WIFI_STATUS: return "WIFI_STATUS";
        case EventType::AP_STATUS: return "AP_STATUS";
        case EventType::IMU_DATA: return "IMU_DATA";
        case EventType::MOTION_COMMAND: return "MOTION_COMMAND";
        case EventType::MOTION_MODE: return "MOTION_MODE";
        case EventType::SERVO_STATE: return "SERVO_STATE";
        case EventType::SYSTEM_BOOT: return "SYSTEM_BOOT";
        case EventType::STORAGE_HYDRATION_COMPLETE: return "STORAGE_HYDRATION_COMPLETE";
        default: return "UNKNOWN";
    }
}

bool isSettingsEvent(EventType type) {
    return static_cast<uint16_t>(type) >= 100 && static_cast<uint16_t>(type) < 200 &&
           static_cast<uint16_t>(type) % 10 == 0;
}
