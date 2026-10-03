#pragma once
// Host stand-in: a fixed factory MAC.
#include <cstdint>
#include <cstring>

inline int esp_efuse_mac_get_default(uint8_t *mac) {
    const uint8_t fixed[6] = {0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF};
    std::memcpy(mac, fixed, 6);
    return 0;
}
