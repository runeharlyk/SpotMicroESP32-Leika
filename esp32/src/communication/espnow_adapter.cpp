#include <communication/espnow_adapter.h>

#if SOC_WIFI_SUPPORTED

#include <communication/handheld_controller.h>
#include <esp_log.h>
#include <atomic>
#include <cstring>

static const char *TAG = "ESP-NOW";

static const uint8_t BROADCAST[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// The receive callback runs on the WiFi task and the beacon on the timer task, neither of which can carry the adapter.
static EspNowAdapter *adapter = nullptr;
static std::atomic<bool> heard {false};
// Touched by the receive callback only, and reset by start() before that callback is registered.
static ControllerLock controllerLock;
// Only the beacon touches these once it runs; start() sets them before it does.
static wifi_interface_t peerInterface = WIFI_IF_STA;
static uint8_t announcedChannel = 0;

// The broadcast goes out on the station's interface while it runs and on the access point's otherwise; an interface
// that is down refuses the send.
static wifi_interface_t interfaceFor(wifi_mode_t mode) { return (mode & WIFI_MODE_STA) ? WIFI_IF_STA : WIFI_IF_AP; }

void EspNowAdapter::follow(bool enabled) {
    if (!enabled) _startFailed = false;
    if (enabled == _running) return;
    if (enabled) _running = start();
    else stop();
}

bool EspNowAdapter::start() {
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) return false;

    adapter = this;
    if (esp_err_t err = esp_now_init(); err != ESP_OK) {
        if (!_startFailed) ESP_LOGE(TAG, "Not listening for the controller: %s", esp_err_to_name(err));
        _startFailed = true;
        return false;
    }
    heard = false;
    controllerLock.reset();
    peerInterface = interfaceFor(mode);
    announcedChannel = 0;
    esp_now_register_recv_cb(onReceive);

    // Channel 0 is whatever channel the radio is on, so the beacon follows it through every join.
    esp_now_peer_info_t peer {};
    std::memcpy(peer.peer_addr, BROADCAST, ESP_NOW_ETH_ALEN);
    peer.channel = 0;
    peer.ifidx = peerInterface;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) ESP_LOGW(TAG, "Broadcast peer not added: no beacon");

    const esp_timer_create_args_t beaconArgs {.callback = &EspNowAdapter::sendBeacon,
                                              .arg = nullptr,
                                              .dispatch_method = ESP_TIMER_TASK,
                                              .name = "espnow_beacon",
                                              .skip_unhandled_events = true};
    if (esp_timer_create(&beaconArgs, &_beacon) == ESP_OK)
        esp_timer_start_periodic(_beacon, HEXAPOD_BEACON_PERIOD_MS * 1000);
    ESP_LOGI(TAG, "Listening for the handheld controller");
    return true;
}

void EspNowAdapter::stop() {
    if (_beacon) {
        esp_timer_stop(_beacon);
        esp_timer_delete(_beacon);
        _beacon = nullptr;
    }
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    _running = false;
    ESP_LOGI(TAG, "Stopped listening for the handheld controller");
}

void EspNowAdapter::onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    // Once per start, so a controller that is not heard can be told apart from one whose frames are refused.
    if (!heard.exchange(true) && info && info->src_addr) {
        const uint8_t *m = info->src_addr;
        ESP_LOGI(TAG, "Controller heard: %02x:%02x:%02x:%02x:%02x:%02x len=%d ver=%u", m[0], m[1], m[2], m[3], m[4],
                 m[5], len, len > 0 ? data[0] : 0);
    }
    if (!info || !info->src_addr) return;
    uint8_t mac[ESP_NOW_ETH_ALEN];
    std::memcpy(mac, info->src_addr, sizeof(mac));
    if (controllerLock.admit(mac, data, len)) adapter->_onFrame(data, len);
}

void EspNowAdapter::sendBeacon(void *) {
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) return;
    const wifi_interface_t interface = interfaceFor(mode);
    if (interface != peerInterface) {
        esp_now_peer_info_t peer {};
        std::memcpy(peer.peer_addr, BROADCAST, ESP_NOW_ETH_ALEN);
        peer.ifidx = interface;
        if (esp_now_mod_peer(&peer) == ESP_OK) peerInterface = interface;
    }

    uint8_t channel = 0;
    wifi_second_chan_t secondary;
    if (esp_wifi_get_channel(&channel, &secondary) != ESP_OK) return;
    if (channel != announcedChannel) {
        ESP_LOGI(TAG, "Beaconing channel %u for the controller", channel);
        announcedChannel = channel;
    }
    const hexapod_beacon_t beacon = beaconFor(channel);
    esp_now_send(BROADCAST, reinterpret_cast<const uint8_t *>(&beacon), sizeof(beacon));
}

#endif
