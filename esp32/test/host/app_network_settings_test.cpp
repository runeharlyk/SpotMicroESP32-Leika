// Host test of settings/app_network_settings.h, built and run by test_host_programs.py.
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <settings/app_network_settings.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

using Network = std::pair<const char *, const char *>;

static api_WifiSettings withNetworks(std::initializer_list<Network> networks) {
    api_WifiSettings settings = api_WifiSettings_init_zero;
    std::strcpy(settings.hostname, "spot");
    for (auto [ssid, password] : networks) {
        api_WifiNetwork &network = settings.wifi_networks[settings.wifi_networks_count++];
        std::strcpy(network.ssid, ssid);
        std::strcpy(network.password, password);
    }
    settings.applied_secret = 0xC0FFEE;
    return settings;
}

static const char *passwordOf(const api_WifiSettings &settings, const char *ssid) {
    for (pb_size_t i = 0; i < settings.wifi_networks_count; i++) {
        if (std::strcmp(settings.wifi_networks[i].ssid, ssid) == 0) return settings.wifi_networks[i].password;
    }
    return nullptr;
}

static api_WifiSettings shownToTheApp(const api_WifiSettings &stored) {
    api_WifiSettings shown;
    WiFiSettings_readForApp(stored, shown);
    return shown;
}

// Any page or device that reaches the socket reads the settings; the passwords and their fingerprint stay put.
static void theAppIsNeverShownAStoredPassword() {
    const api_WifiSettings stored = withNetworks({{"Home", "correct horse"}, {"Cafe", ""}});
    const api_WifiSettings shown = shownToTheApp(stored);
    CHECK(std::strcmp(passwordOf(shown, "Home"), STORED_PASSWORD_MASK) == 0);
    CHECK(std::strcmp(passwordOf(shown, "Cafe"), "") == 0);
    CHECK(shown.applied_secret == 0);
    CHECK(std::strcmp(shown.hostname, "spot") == 0);
}

// The app saves the form it was shown: every password it did not touch stays as stored.
static void savingWhatWasShownChangesNothing() {
    api_WifiSettings stored = withNetworks({{"Home", "correct horse"}, {"Cafe", ""}});
    const api_WifiSettings before = stored;
    CHECK(WiFiSettings_updateFromApp(shownToTheApp(stored), stored) == StateUpdateResult::CHANGED);
    CHECK(std::memcmp(&before, &stored, sizeof(stored)) == 0);
}

static void aNewPasswordReplacesTheStoredOneAndAnEmptyOneOpensTheNetwork() {
    api_WifiSettings stored = withNetworks({{"Home", "correct horse"}, {"Work", "battery staple"}});
    api_WifiSettings saved = shownToTheApp(stored);
    std::strcpy(saved.wifi_networks[0].password, "new password");
    std::strcpy(saved.wifi_networks[1].password, "");
    CHECK(WiFiSettings_updateFromApp(saved, stored) == StateUpdateResult::CHANGED);
    CHECK(std::strcmp(passwordOf(stored, "Home"), "new password") == 0);
    CHECK(std::strcmp(passwordOf(stored, "Work"), "") == 0);
}

// Networks are matched by name, not by position, so reordering or deleting keeps the right passwords.
static void reorderedAndRemainingNetworksKeepTheirPasswords() {
    api_WifiSettings stored = withNetworks({{"Home", "correct horse"}, {"Work", "battery staple"}, {"Cafe", "latte"}});
    api_WifiSettings saved = withNetworks({{"Cafe", STORED_PASSWORD_MASK}, {"Home", STORED_PASSWORD_MASK}});
    CHECK(WiFiSettings_updateFromApp(saved, stored) == StateUpdateResult::CHANGED);
    CHECK(stored.wifi_networks_count == 2);
    CHECK(std::strcmp(stored.wifi_networks[0].ssid, "Cafe") == 0);
    CHECK(std::strcmp(passwordOf(stored, "Cafe"), "latte") == 0);
    CHECK(std::strcmp(passwordOf(stored, "Home"), "correct horse") == 0);
}

// A renamed network's password cannot be the old network's: the save is refused and nothing changes.
static void keepingAPasswordThatWasNeverStoredIsRefused() {
    api_WifiSettings stored = withNetworks({{"Home", "correct horse"}});
    const api_WifiSettings before = stored;
    api_WifiSettings saved = shownToTheApp(stored);
    std::strcpy(saved.wifi_networks[0].ssid, "Home 5G");
    CHECK(WiFiSettings_updateFromApp(saved, stored) == StateUpdateResult::ERROR);
    CHECK(std::memcmp(&before, &stored, sizeof(stored)) == 0);
}

// The fingerprint of the network merged from secrets.h is the firmware's: losing it would re-add that network.
static void theMergedSecretsFingerprintSurvivesASave() {
    api_WifiSettings stored = withNetworks({{"Home", "correct horse"}});
    CHECK(WiFiSettings_updateFromApp(shownToTheApp(stored), stored) == StateUpdateResult::CHANGED);
    CHECK(stored.applied_secret == 0xC0FFEE);
}

static api_APSettings accessPoint(const char *password) {
    api_APSettings settings = api_APSettings_init_zero;
    std::strcpy(settings.ssid, "Spot");
    std::strcpy(settings.password, password);
    settings.channel = 6;
    return settings;
}

static void theAccessPointPasswordIsHiddenAndKeptTheSameWay() {
    api_APSettings stored = accessPoint("spot-leika");
    api_APSettings shown;
    APSettings_readForApp(stored, shown);
    CHECK(std::strcmp(shown.password, STORED_PASSWORD_MASK) == 0);
    CHECK(shown.channel == 6);

    shown.channel = 11;
    CHECK(APSettings_updateFromApp(shown, stored) == StateUpdateResult::CHANGED);
    CHECK(std::strcmp(stored.password, "spot-leika") == 0);
    CHECK(stored.channel == 11);

    api_APSettings open = accessPoint("");
    APSettings_readForApp(open, shown);
    CHECK(std::strcmp(shown.password, "") == 0);
    std::strcpy(shown.password, "new secret");
    CHECK(APSettings_updateFromApp(shown, open) == StateUpdateResult::CHANGED);
    CHECK(std::strcmp(open.password, "new secret") == 0);
}

int main() {
    theAppIsNeverShownAStoredPassword();
    savingWhatWasShownChangesNothing();
    aNewPasswordReplacesTheStoredOneAndAnEmptyOneOpensTheNetwork();
    reorderedAndRemainingNetworksKeepTheirPasswords();
    keepingAPasswordThatWasNeverStoredIsRefused();
    theMergedSecretsFingerprintSurvivesASave();
    theAccessPointPasswordIsHiddenAndKeptTheSameWay();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
