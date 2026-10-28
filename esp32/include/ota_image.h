#pragma once

#include <cstddef>
#include <cstdint>

// What the start of an application image looks like (ESP-IDF app image format): the image header's
// 0xE9, then esp_app_desc_t right after the 24-byte image header and the first 8-byte segment header.
// A factory image also starts with 0xE9 -- its bootloader -- but carries no app descriptor there, and
// written into an app slot it would only boot into a rollback.
namespace ota_image {

constexpr size_t APP_DESC_OFFSET = 24 + 8;
constexpr size_t HEAD_SIZE = APP_DESC_OFFSET + 4;
constexpr uint8_t IMAGE_MAGIC = 0xE9;
constexpr uint32_t APP_DESC_MAGIC = 0xABCD5432;

inline bool isAppImage(const uint8_t *head, size_t len) {
    if (len < HEAD_SIZE || head[0] != IMAGE_MAGIC) return false;
    const uint8_t *m = head + APP_DESC_OFFSET;
    const uint32_t magic = m[0] | (m[1] << 8) | (m[2] << 16) | (uint32_t(m[3]) << 24);
    return magic == APP_DESC_MAGIC;
}

}  // namespace ota_image
