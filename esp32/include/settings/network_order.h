#pragma once

#include <platform_shared/api.pb.h>
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>

/** An access point a scan heard. */
struct SeenNetwork {
    const char *ssid;
    int rssi;
};

/**
 * The order to try the saved networks in when the strongest is preferred: those the scan heard, strongest first by
 * their best access point, then the rest in saved order, since a scan can miss a network that is there.
 * Writes every saved index to `order` and returns how many.
 */
inline size_t orderBySignal(const api_WifiSettings &settings, const SeenNetwork *seen, size_t seenCount,
                            uint32_t *order) {
    const size_t count = settings.wifi_networks_count;
    int best[sizeof(settings.wifi_networks) / sizeof(settings.wifi_networks[0])];
    for (size_t i = 0; i < count; i++) {
        best[i] = INT_MIN;
        order[i] = i;
        for (size_t s = 0; s < seenCount; s++)
            if (std::strcmp(settings.wifi_networks[i].ssid, seen[s].ssid) == 0) best[i] = std::max(best[i], seen[s].rssi);
    }
    std::stable_sort(order, order + count, [&best](uint32_t a, uint32_t b) { return best[a] > best[b]; });
    return count;
}
