#pragma once

#include <template/state_result.h>
#include <platform_shared/api.pb.h>
#include <cstring>

/**
 * How the app reads and saves the network settings. Whatever reaches the socket can read them, so stored
 * passwords are shown as this mask, and a save that sends the mask back keeps the stored password: the
 * convention of routers and of other device firmware. A password of exactly the mask cannot be set.
 * Flash keeps the passwords themselves; see WiFiSettings_read and APSettings_read.
 */
constexpr const char *STORED_PASSWORD_MASK = "********";

namespace app_network_detail {

template <size_t N>
inline void maskStored(char (&password)[N]) {
    if (password[0] != '\0') strncpy(password, STORED_PASSWORD_MASK, N - 1);
}

inline bool isMask(const char *password) { return strcmp(password, STORED_PASSWORD_MASK) == 0; }

inline const api_WifiNetwork *storedNetwork(const api_WifiSettings &settings, const char *ssid) {
    for (pb_size_t i = 0; i < settings.wifi_networks_count; i++) {
        if (strcmp(settings.wifi_networks[i].ssid, ssid) == 0) return &settings.wifi_networks[i];
    }
    return nullptr;
}

} // namespace app_network_detail

// The fingerprint of the network merged from secrets.h is hidden too: it hashes that network's password.
inline void WiFiSettings_readForApp(const api_WifiSettings &settings, api_WifiSettings &proto) {
    proto = settings;
    proto.applied_secret = 0;
    for (pb_size_t i = 0; i < proto.wifi_networks_count; i++) {
        app_network_detail::maskStored(proto.wifi_networks[i].password);
    }
}

/**
 * Networks keep their passwords by name, so the app may reorder and delete them. A mask on a network
 * with no stored password, such as a renamed one, is refused. The app does not own the merged secret's
 * fingerprint, and losing it would re-add a deleted network.
 */
inline StateUpdateResult WiFiSettings_updateFromApp(const api_WifiSettings &proto, api_WifiSettings &settings) {
    api_WifiSettings next = proto;
    for (pb_size_t i = 0; i < next.wifi_networks_count; i++) {
        api_WifiNetwork &network = next.wifi_networks[i];
        if (!app_network_detail::isMask(network.password)) continue;
        const api_WifiNetwork *stored = app_network_detail::storedNetwork(settings, network.ssid);
        if (!stored) return StateUpdateResult::ERROR;
        memcpy(network.password, stored->password, sizeof(network.password));
    }
    next.applied_secret = settings.applied_secret;
    settings = next;
    return StateUpdateResult::CHANGED;
}

inline void APSettings_readForApp(const api_APSettings &settings, api_APSettings &proto) {
    proto = settings;
    app_network_detail::maskStored(proto.password);
}

inline StateUpdateResult APSettings_updateFromApp(const api_APSettings &proto, api_APSettings &settings) {
    api_APSettings next = proto;
    if (app_network_detail::isMask(next.password)) memcpy(next.password, settings.password, sizeof(next.password));
    settings = next;
    return StateUpdateResult::CHANGED;
}
