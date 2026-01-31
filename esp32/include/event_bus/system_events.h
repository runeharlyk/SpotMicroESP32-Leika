#pragma once
#include <cstdint>

enum class SystemEventType : uint8_t {
    STORAGE_HYDRATION_COMPLETE,
    SYSTEM_BOOT_COMPLETE,
};

struct SystemEvent {
    SystemEventType type;
};
