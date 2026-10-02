#pragma once

#include <platform_shared/api.pb.h>
#include <cstdint>
#include <cstring>

/** FNV-1a over the SSID and password; never 0, which stands for "nothing merged yet". */
inline uint32_t factoryNetworkFingerprint(const char *ssid, const char *password) {
    uint32_t hash = 2166136261u;
    auto mix = [&hash](const char *text) {
        for (const char *c = text;; c++) {
            hash = (hash ^ static_cast<uint8_t>(*c)) * 16777619u;
            if (*c == '\0') break;
        }
    };
    mix(ssid);
    mix(password);
    return hash == 0 ? 1 : hash;
}

/**
 * Merges the network compiled in from secrets.h into the stored list, once per change of the secret:
 * a new SSID is appended, a known SSID gets the new password, and a network the user deleted in the
 * app stays deleted until the secret changes. A full list is left alone and tried again next boot.
 * Returns whether the settings changed.
 */
inline bool applyFactoryNetwork(api_WifiSettings &settings, const char *ssid, const char *password) {
    if (ssid[0] == '\0') return false;
    uint32_t fingerprint = factoryNetworkFingerprint(ssid, password);
    if (settings.applied_secret == fingerprint) return false;

    constexpr size_t capacity = sizeof(settings.wifi_networks) / sizeof(settings.wifi_networks[0]);
    api_WifiNetwork *network = nullptr;
    for (size_t i = 0; i < settings.wifi_networks_count; i++) {
        if (std::strncmp(settings.wifi_networks[i].ssid, ssid, sizeof(network->ssid) - 1) == 0)
            network = &settings.wifi_networks[i];
    }
    if (!network) {
        if (settings.wifi_networks_count >= capacity) return false;
        network = &settings.wifi_networks[settings.wifi_networks_count++];
        *network = api_WifiNetwork_init_zero;
        std::strncpy(network->ssid, ssid, sizeof(network->ssid) - 1);
    }
    std::strncpy(network->password, password, sizeof(network->password) - 1);
    network->password[sizeof(network->password) - 1] = '\0';
    settings.applied_secret = fingerprint;
    return true;
}
