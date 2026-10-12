// Host test of settings/network_order.h, built and run by test_host_programs.py.
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <settings/network_order.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static api_WifiSettings withNetworks(std::initializer_list<const char *> ssids) {
    api_WifiSettings settings = api_WifiSettings_init_zero;
    for (const char *ssid : ssids) std::strcpy(settings.wifi_networks[settings.wifi_networks_count++].ssid, ssid);
    return settings;
}

static bool orderIs(const uint32_t *order, size_t count, std::initializer_list<uint32_t> expected) {
    if (count != expected.size()) return false;
    size_t i = 0;
    for (uint32_t index : expected)
        if (order[i++] != index) return false;
    return true;
}

static void strongestHeardComesFirst() {
    api_WifiSettings settings = withNetworks({"Home", "Lab", "Phone"});
    SeenNetwork seen[] = {{"Home", -78}, {"Phone", -50}, {"Lab", -64}};
    uint32_t order[5];
    CHECK(orderIs(order, orderBySignal(settings, seen, 3, order), {2, 1, 0}));
}

static void aNetworkRanksByItsStrongestAccessPoint() {
    api_WifiSettings settings = withNetworks({"Home", "Lab"});
    SeenNetwork seen[] = {{"Home", -85}, {"Lab", -60}, {"Home", -40}};
    uint32_t order[5];
    CHECK(orderIs(order, orderBySignal(settings, seen, 3, order), {0, 1}));
}

static void unheardNetworksFollowInSavedOrder() {
    api_WifiSettings settings = withNetworks({"Home", "Lab", "Phone", "Office"});
    SeenNetwork seen[] = {{"Office", -70}, {"Neighbour", -30}};
    uint32_t order[5];
    CHECK(orderIs(order, orderBySignal(settings, seen, 2, order), {3, 0, 1, 2}));
}

static void anEmptyScanKeepsTheSavedOrder() {
    api_WifiSettings settings = withNetworks({"Home", "Lab"});
    uint32_t order[5];
    CHECK(orderIs(order, orderBySignal(settings, nullptr, 0, order), {0, 1}));
}

static void equalSignalsKeepTheSavedOrder() {
    api_WifiSettings settings = withNetworks({"Home", "Lab"});
    SeenNetwork seen[] = {{"Lab", -60}, {"Home", -60}};
    uint32_t order[5];
    CHECK(orderIs(order, orderBySignal(settings, seen, 2, order), {0, 1}));
}

int main() {
    strongestHeardComesFirst();
    aNetworkRanksByItsStrongestAccessPoint();
    unheardNetworksFollowInSavedOrder();
    anEmptyScanKeepsTheSavedOrder();
    equalSignalsKeepTheSavedOrder();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
