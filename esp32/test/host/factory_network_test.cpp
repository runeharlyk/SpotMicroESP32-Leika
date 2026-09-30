// Host test of settings/factory_network.h, built and run by test_factory_network.py.
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <settings/factory_network.h>

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
    for (const char *ssid : ssids) {
        std::strcpy(settings.wifi_networks[settings.wifi_networks_count].ssid, ssid);
        std::strcpy(settings.wifi_networks[settings.wifi_networks_count].password, "old-password");
        settings.wifi_networks_count++;
    }
    return settings;
}

static void noSecretChangesNothing() {
    api_WifiSettings settings = withNetworks({"Office"});
    CHECK(!applyFactoryNetwork(settings, "", ""));
    CHECK(settings.wifi_networks_count == 1);
}

static void aNewSecretIsAddedOnce() {
    api_WifiSettings settings = withNetworks({"Office"});
    CHECK(applyFactoryNetwork(settings, "HomeNet", "secret-1"));
    CHECK(settings.wifi_networks_count == 2);
    CHECK(std::strcmp(settings.wifi_networks[1].ssid, "HomeNet") == 0);
    CHECK(std::strcmp(settings.wifi_networks[1].password, "secret-1") == 0);
    CHECK(!applyFactoryNetwork(settings, "HomeNet", "secret-1"));
    CHECK(settings.wifi_networks_count == 2);
}

static void aDeletedNetworkStaysDeleted() {
    api_WifiSettings settings = withNetworks({});
    applyFactoryNetwork(settings, "HomeNet", "secret-1");
    settings.wifi_networks_count = 0;  // removed in the app
    CHECK(!applyFactoryNetwork(settings, "HomeNet", "secret-1"));
    CHECK(settings.wifi_networks_count == 0);
}

static void aChangedPasswordUpdatesTheStoredNetwork() {
    api_WifiSettings settings = withNetworks({"Office", "HomeNet"});
    CHECK(applyFactoryNetwork(settings, "HomeNet", "secret-2"));
    CHECK(settings.wifi_networks_count == 2);
    CHECK(std::strcmp(settings.wifi_networks[1].password, "secret-2") == 0);
    CHECK(std::strcmp(settings.wifi_networks[0].password, "old-password") == 0);
}

static void aChangedSecretReturnsAfterDeletion() {
    api_WifiSettings settings = withNetworks({});
    applyFactoryNetwork(settings, "HomeNet", "secret-1");
    settings.wifi_networks_count = 0;
    CHECK(applyFactoryNetwork(settings, "HomeNet", "secret-2"));
    CHECK(settings.wifi_networks_count == 1);
}

static void aFullListIsLeftAloneAndRetriedLater() {
    api_WifiSettings settings = withNetworks({"A", "B", "C", "D", "E"});
    CHECK(!applyFactoryNetwork(settings, "HomeNet", "secret-1"));
    CHECK(settings.wifi_networks_count == 5);
    settings.wifi_networks_count = 4;  // a slot freed in the app
    CHECK(applyFactoryNetwork(settings, "HomeNet", "secret-1"));
    CHECK(std::strcmp(settings.wifi_networks[4].ssid, "HomeNet") == 0);
}

static void anOverlongSecretIsCutToFit() {
    api_WifiSettings settings = withNetworks({});
    char ssid[64];
    std::memset(ssid, 'x', sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    CHECK(applyFactoryNetwork(settings, ssid, "secret-1"));
    CHECK(std::strlen(settings.wifi_networks[0].ssid) == sizeof(settings.wifi_networks[0].ssid) - 1);
}

int main() {
    noSecretChangesNothing();
    aNewSecretIsAddedOnce();
    aDeletedNetworkStaysDeleted();
    aChangedPasswordUpdatesTheStoredNetwork();
    aChangedSecretReturnsAfterDeletion();
    aFullListIsLeftAloneAndRetriedLater();
    anOverlongSecretIsCutToFit();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
