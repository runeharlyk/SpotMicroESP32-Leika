#pragma once

#include <driver/gpio.h>
#include <peripherals/sonar_ranging.h>

#include <array>
#include <atomic>

/**
 * The HC-SR04s' GPIOs, as sonar::Ranger drives them: a trigger output each, and an echo input whose edges an interrupt
 * stamps with esp_timer, so the sensor task never waits on an echo.
 *
 * The echo is 5 V: a 3.3 V GPIO needs a divider on it, such as 1 kOhm from the echo to the pin and 2 kOhm from the
 * pin to ground. The trigger takes 3.3 V as it is.
 */
class SonarPins {
  public:
    struct Wiring {
        bool enabled = false;
        gpio_num_t trigger = GPIO_NUM_NC, echo = GPIO_NUM_NC;
        bool operator==(const Wiring &) const = default;
    };

    /** Releases the pins of the last wiring and takes those of the enabled sensors in `wiring`. */
    void attach(const std::array<Wiring, sonar::COUNT> &wiring);
    // Whether the last attach took the sensor's pins.
    bool attached(int sensor) const { return _wiring[sensor].enabled; }

    void trigger(int sensor);
    bool echoHigh(int sensor) const;
    sonar::Echo echo(int sensor) const;

  private:
    // Written by the interrupt: each time before its flag, which the reader takes first.
    struct Capture {
        gpio_num_t pin = GPIO_NUM_NC;
        std::atomic<bool> rose {false}, fell {false};
        std::atomic<uint32_t> riseUs {0}, fallUs {0};
    };
    static void onEdge(void *capture);

    std::array<Wiring, sonar::COUNT> _wiring {};
    std::array<Capture, sonar::COUNT> _captures {};
};
