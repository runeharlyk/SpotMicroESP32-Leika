// Host test of a settings group as a state topic (settings/settings_topic.h) on CommAdapterBase, with the push on every
// stored change that main.cpp registers; built and run by test_host_programs.py.
#include <cstdio>
#include <cstring>
#include <string>
#include "fake_link.h"
#include <settings/app_network_settings.h>
#include <settings/settings_topic.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

namespace {

constexpr int32_t WIFI_TAG = socket_message_Message_stored_wifi_settings_tag;

// The WiFi settings as the WiFi service holds them, read and written as the app reads and writes them.
struct WifiService : StatefulService<api_WifiSettings> {
    StatefulProtoHandler<api_WifiSettings, api_WifiSettings> protoHandler {
        WiFiSettings_readForApp, WiFiSettings_updateFromApp, this, api_WifiSettings_fields};
};

WifiService wifi;
SettingsTopic<api_WifiSettings> wifiTopic {wifi.protoHandler};

constexpr Topic TOPICS[] = {
    {WIFI_TAG, TopicKind::STATE, [](SnapshotSink &sink) { wifiTopic.snapshot(sink); }},
};

api_WifiSettings withNetwork(const char *ssid, const char *password, const char *hostname = "spot") {
    api_WifiSettings settings = api_WifiSettings_init_zero;
    std::strncpy(settings.hostname, hostname, sizeof(settings.hostname) - 1);
    settings.wifi_networks_count = 1;
    std::strncpy(settings.wifi_networks[0].ssid, ssid, sizeof(settings.wifi_networks[0].ssid) - 1);
    std::strncpy(settings.wifi_networks[0].password, password, sizeof(settings.wifi_networks[0].password) - 1);
    return settings;
}

std::vector<Frame> settingsTo(FakeLink &link, int cid) {
    std::vector<Frame> frames;
    for (const Frame &frame : link.to(cid))
        if (frame.message.which_message == WIFI_TAG) frames.push_back(frame);
    return frames;
}

} // namespace

// A subscribe is answered with the stored group as a reply carries it, the password masked, then the ack.
static void aSubscribeIsAnsweredWithTheStoredGroupMaskedThenTheAck() {
    FakeLink link(TOPICS);
    wifi.protoHandler.update(withNetwork("home", "secret-1"));

    link.subscribeTo(WIFI_TAG, 3);

    const std::vector<Frame> frames = link.to(3);
    CHECK(frames.size() == 2);
    if (frames.size() != 2) return;
    const api_WifiSettings &sent = frames[0].message.message.stored_wifi_settings;
    CHECK(frames[0].message.which_message == WIFI_TAG);
    CHECK(std::string(sent.wifi_networks[0].ssid) == "home");
    CHECK(std::string(sent.wifi_networks[0].password) == STORED_PASSWORD_MASK);
    CHECK(frames[1].message.which_message == socket_message_Message_sub_notif_tag);
}

// Every client sees a change, whichever made it; a save that stores nothing new pushes nothing, and its reply settles
// the write.
static void aStoredChangeIsPushedToEverySubscriberAndNothingElseIs() {
    FakeLink link(TOPICS);
    const HandlerId pushing = wifi.addUpdateHandler([&link](const std::string &) { link.push(WIFI_TAG); });
    wifi.protoHandler.update(withNetwork("home", "secret-1"));
    link.subscribeTo(WIFI_TAG, 3);
    link.subscribeTo(WIFI_TAG, 4);
    link.forget();

    CHECK(wifi.protoHandler.update(withNetwork("home", STORED_PASSWORD_MASK)) == StateUpdateResult::UNCHANGED);
    CHECK(link.sentCount() == 0);

    CHECK(wifi.protoHandler.update(withNetwork("home", STORED_PASSWORD_MASK, "leika")) == StateUpdateResult::CHANGED);
    CHECK(settingsTo(link, 3).size() == 1 && settingsTo(link, 4).size() == 1);
    if (!settingsTo(link, 4).empty()) {
        const api_WifiSettings &sent = settingsTo(link, 4)[0].message.message.stored_wifi_settings;
        CHECK(std::string(sent.hostname) == "leika");
        CHECK(std::string(sent.wifi_networks[0].password) == STORED_PASSWORD_MASK);
    }
    wifi.removeUpdateHandler(pushing);
}

int main() {
    aSubscribeIsAnsweredWithTheStoredGroupMaskedThenTheAck();
    aStoredChangeIsPushedToEverySubscriberAndNothingElseIs();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
