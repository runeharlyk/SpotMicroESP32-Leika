#include <peripherals/sonar.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>

static const char *const TAG = "Sonar";

// The HC-SR04 asks for at least 10 us.
static constexpr uint32_t TRIGGER_PULSE_US = 10;

void IRAM_ATTR SonarPins::onEdge(void *arg) {
    auto *capture = static_cast<Capture *>(arg);
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time());
    if (gpio_get_level(capture->pin)) {
        capture->riseUs.store(now);
        capture->rose.store(true);
    } else if (capture->rose.load()) {
        capture->fallUs.store(now);
        capture->fell.store(true);
    }
}

void SonarPins::attach(const std::array<Wiring, sonar::COUNT> &wiring) {
    for (int i = 0; i < sonar::COUNT; i++) {
        if (!_wiring[i].enabled) continue;
        gpio_isr_handler_remove(_wiring[i].echo);
        gpio_reset_pin(_wiring[i].echo);
        gpio_reset_pin(_wiring[i].trigger);
    }
    _wiring = wiring;

    // Installed by another driver already when it reports an invalid state.
    const esp_err_t service = gpio_install_isr_service(0);
    if (service != ESP_OK && service != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "No GPIO interrupt service: %s", esp_err_to_name(service));
        for (Wiring &sensor : _wiring) sensor.enabled = false;
        return;
    }

    for (int i = 0; i < sonar::COUNT; i++) {
        Wiring &sensor = _wiring[i];
        if (!sensor.enabled) continue;
        Capture &capture = _captures[i];
        capture.pin = sensor.echo;
        capture.rose = capture.fell = false;

        const gpio_config_t trigger {.pin_bit_mask = BIT64(sensor.trigger),
                                     .mode = GPIO_MODE_OUTPUT,
                                     .pull_up_en = GPIO_PULLUP_DISABLE,
                                     .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                     .intr_type = GPIO_INTR_DISABLE};
        // Pulled down, so an echo pin with nothing on it reads as no sensor rather than as noise.
        const gpio_config_t echo {.pin_bit_mask = BIT64(sensor.echo),
                                  .mode = GPIO_MODE_INPUT,
                                  .pull_up_en = GPIO_PULLUP_DISABLE,
                                  .pull_down_en = GPIO_PULLDOWN_ENABLE,
                                  .intr_type = GPIO_INTR_ANYEDGE};
        esp_err_t err = gpio_config(&trigger);
        if (err == ESP_OK) err = gpio_set_level(sensor.trigger, 0);
        if (err == ESP_OK) err = gpio_config(&echo);
        if (err == ESP_OK) err = gpio_isr_handler_add(sensor.echo, onEdge, &capture);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Sonar %d on trigger GPIO %d, echo GPIO %d failed: %s", i, sensor.trigger, sensor.echo,
                     esp_err_to_name(err));
            gpio_reset_pin(sensor.echo);
            gpio_reset_pin(sensor.trigger);
            sensor.enabled = false;
            continue;
        }
        ESP_LOGI(TAG, "Sonar %d on trigger GPIO %d, echo GPIO %d", i, sensor.trigger, sensor.echo);
    }
}

void SonarPins::trigger(int sensor) {
    if (!_wiring[sensor].enabled) return;
    Capture &capture = _captures[sensor];
    capture.rose = capture.fell = false;
    gpio_set_level(_wiring[sensor].trigger, 1);
    esp_rom_delay_us(TRIGGER_PULSE_US);
    gpio_set_level(_wiring[sensor].trigger, 0);
}

bool SonarPins::echoHigh(int sensor) const {
    return _wiring[sensor].enabled && gpio_get_level(_wiring[sensor].echo);
}

sonar::Echo SonarPins::echo(int sensor) const {
    const Capture &capture = _captures[sensor];
    sonar::Echo echo;
    echo.rose = capture.rose.load();
    echo.riseUs = capture.riseUs.load();
    echo.fell = capture.fell.load();
    echo.fallUs = capture.fallUs.load();
    return echo;
}
