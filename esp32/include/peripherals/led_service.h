#ifndef LEDService_h
#define LEDService_h

#include <sdkconfig.h>
#include <driver/rmt_tx.h>
#include <led_strip.h>
#include <led_strip_rmt.h>
#include <wifi/wifi_idf.h>
#include <utils/timing.h>
#include <esp_log.h>
#include <atomic>

#ifndef WS2812_NUM_LEDS
#define WS2812_NUM_LEDS 13
#endif

class LEDService {
  private:
    static constexpr int32_t NO_STRIP = -1;

    led_strip_handle_t led_strip = nullptr;
    int _brightness = 255;
    int direction = 1;
    bool initialized = false;
    std::atomic<int32_t> _requestedPin {NO_STRIP};
    int32_t _appliedPin = NO_STRIP;

    struct RGB {
        uint8_t r, g, b;
    };

    RGB oceanColor = {0, 119, 190};
    RGB forestColor = {34, 139, 34};

  public:
    LEDService() {}

    ~LEDService() {
        if (initialized && led_strip) {
            led_strip_del(led_strip);
        }
    }

    /**
     * The strip to drive, from any task: on `pin`, or none for -1. The control task applies it in loop(), since it
     * drives the strip.
     */
    void drive(int32_t pin) { _requestedPin.store(pin); }

    void loop() {
        applyRequestedPin();
        if (!initialized) return;

        EXECUTE_EVERY_N_MS(1000 / 60, {
            if (_brightness >= 200) direction = -5;
            if (_brightness <= 50) direction = 5;
            _brightness += direction;

            RGB color = WiFi.isConnected() ? oceanColor : forestColor;

            uint8_t r = (color.r * _brightness) / 255;
            uint8_t g = (color.g * _brightness) / 255;
            uint8_t b = (color.b * _brightness) / 255;

            for (int i = 0; i < WS2812_NUM_LEDS; ++i) {
                led_strip_set_pixel(led_strip, i, r, g, b);
            }
            led_strip_refresh(led_strip);
        });
    }

  private:
    void applyRequestedPin() {
        const int32_t pin = _requestedPin.load();
        if (pin == _appliedPin) return;
        _appliedPin = pin;
        if (initialized) {
            led_strip_clear(led_strip);
            led_strip_del(led_strip);
            led_strip = nullptr;
            initialized = false;
        }
        if (pin != NO_STRIP) begin(pin);
    }

    void begin(int32_t pin) {
        led_strip_config_t strip_config = {
            .strip_gpio_num = pin,
            .max_leds = WS2812_NUM_LEDS,
            .led_pixel_format = LED_PIXEL_FORMAT_GRB,
            .led_model = LED_MODEL_WS2812,
            .flags = {.invert_out = false},
        };

        led_strip_rmt_config_t rmt_config = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000,
            .mem_block_symbols = 64,
            .flags = {.with_dma = false},
        };

        esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
        if (err == ESP_OK) {
            initialized = true;
            led_strip_clear(led_strip);
            ESP_LOGI("LEDService", "LED strip initialized on GPIO %d", static_cast<int>(pin));
        } else {
            ESP_LOGE("LEDService", "Failed to initialize LED strip: %s", esp_err_to_name(err));
        }
    }
};

#endif
