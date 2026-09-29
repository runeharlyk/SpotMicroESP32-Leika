#pragma once

#include <esp_mac.h>
#include <sdkconfig.h>
#include <utils/identity.hpp>

// The factory base MAC is burned into eFuse, so it identifies the board across reflashes and resets.
inline const std::string &deviceId() {
    static const std::string id = [] {
        uint8_t mac[6] = {};
        esp_efuse_mac_get_default(mac);
        return formatDeviceId(mac);
    }();
    return id;
}

// "#{platform}" becomes the IDF target (e.g. "esp32s3") and "#{unique_id}" the last six hex digits of the device id.
inline std::string substitutePlaceholders(const char *tmpl) {
    return expandPlaceholders(tmpl, CONFIG_IDF_TARGET, deviceId().substr(6));
}
