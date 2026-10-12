#pragma once
// Host stand-in: the LittleFS mount is firmware-only; host tests use the host file system.
#include <cstddef>
#include <esp_err.h>

// Declared only; a test that links code asking for the mount's size defines it.
esp_err_t esp_littlefs_info(const char *partition_label, size_t *total_bytes, size_t *used_bytes);
