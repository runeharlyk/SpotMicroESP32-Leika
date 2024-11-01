#pragma once

#include <Arduino.h>

constexpr static const char* PLATFORM = "esp32";

static String getRandom() { return String(random(2147483647), HEX); }

static String getUniqueId() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macStr[13] = {0};
    sprintf(macStr, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

static String replaceEach(String value, String pattern, String (*generateReplacement)()) {
    while (true) {
        int index = value.indexOf(pattern);
        if (index == -1) {
            break;
        }
        value = value.substring(0, index) + generateReplacement() + value.substring(index + pattern.length());
    }
    return value;
}

static String format(String value) {
    value = replaceEach(value, "#{random}", getRandom);
    value.replace("#{unique_id}", getUniqueId());
    value.replace("#{platform}", PLATFORM);
    return value;
}