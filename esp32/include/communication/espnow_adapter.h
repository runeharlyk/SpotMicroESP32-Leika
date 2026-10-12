#pragma once

#include <soc/soc_caps.h>

// The ESP32-P4 reaches WiFi through a coprocessor and has no radio of its own for ESP-NOW.
#if SOC_WIFI_SUPPORTED

#include <esp_now.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <functional>
#include <utility>

/**
 * Listens for the ESP-NOW handheld controller and hands each frame it hears on; it only ever sends its beacon.
 * The controller broadcasts and the robot takes frames from any sender, as Hexapod does: there is no pairing.
 *
 * ESP-NOW only hears the channel the radio is on, and one radio serves the station, the access point and ESP-NOW, so the
 * channel is the router's while the station is joined and the access point's otherwise. Hexapod pins the radio to a
 * fixed channel when the station is not joined; here that would move the access point off its configured channel and
 * drop the app. Instead the robot broadcasts its channel every HEXAPOD_BEACON_PERIOD_MS and the controller hunts for
 * it. The beacon reads the channel at every send, so it follows every join and drop of the station, and a change of
 * channel is logged.
 */
class EspNowAdapter {
  public:
    using FrameHandler = std::function<void(const uint8_t *data, int len)>;

    explicit EspNowAdapter(FrameHandler onFrame) : _onFrame(std::move(onFrame)) {}

    /**
     * Starts or stops listening to match `enabled`, from one task only and after WiFi.init(). A start waits until the
     * WiFi driver runs, so calling this on every pass of a loop also retries it.
     */
    void follow(bool enabled);

  private:
    bool start();
    void stop();
    static void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len);
    static void sendBeacon(void *);

    FrameHandler _onFrame;
    bool _running = false;
    bool _startFailed = false;
    esp_timer_handle_t _beacon = nullptr;
};

#endif
