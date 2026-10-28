#pragma once

#include <array>
#include <cstdint>
#include <optional>

/**
 * Ranging with two HC-SR04 sonars, apart from the GPIOs: a pulse on a sensor's trigger pin sends a burst, and its echo
 * pin then stays high as long as the sound took there and back. The sensors take turns, so neither hears the other's
 * burst, and a ping that finds nothing within 4 m reads as no echo rather than as a distance.
 *
 * Times are esp_timer microseconds cut to 32 bits; they wrap every 71 minutes, which differences survive.
 */
namespace sonar {

constexpr int COUNT = 2;
// Sound at 20 C, there and back: 58 us of echo per cm.
constexpr float CM_PER_ECHO_US = 0.0343f / 2;
// The longest echo taken as a distance: 25 ms is 4.3 m, past the sensor's 4 m.
constexpr uint32_t ECHO_TIMEOUT_US = 25000;
// The echo starts some 0.5 ms after the trigger; one not started by this has no sensor behind it.
constexpr uint32_t ECHO_START_TIMEOUT_US = 5000;
// Each slot pings one sensor. It outlasts the slowest answer, a late start and a full timeout, so a ping is always
// settled before the next, and lets the burst die away: two sensors get 10 pings a second each, inside the 60 ms
// the datasheet asks between pings of one.
constexpr uint32_t SLOT_US = 50000;
static_assert(SLOT_US > ECHO_START_TIMEOUT_US + ECHO_TIMEOUT_US);

enum class Status : uint8_t { Off, Distance, NoEcho, NoSensor };

struct Reading {
    Status status = Status::Off;
    float distanceCm = 0;
    bool operator==(const Reading &) const = default;
};

/** The echo pin's edges since the trigger. */
struct Echo {
    bool rose = false, fell = false;
    uint32_t riseUs = 0, fallUs = 0;
};

inline float distanceCm(uint32_t echoUs) { return echoUs * CM_PER_ECHO_US; }

/**
 * What a ping triggered at `triggeredUs` found by `nowUs`, or nothing while its echo may still come. A module that
 * hears nothing still raises its echo, for some 38 ms or until its own timeout: that is no echo, not 6 m.
 */
inline std::optional<Reading> evaluate(uint32_t triggeredUs, const Echo &echo, uint32_t nowUs) {
    if (echo.rose && echo.fell) {
        const uint32_t width = echo.fallUs - echo.riseUs;
        if (width > ECHO_TIMEOUT_US) return Reading {Status::NoEcho};
        return Reading {Status::Distance, distanceCm(width)};
    }
    if (echo.rose) {
        if (nowUs - echo.riseUs > ECHO_TIMEOUT_US) return Reading {Status::NoEcho};
        return std::nullopt;
    }
    if (nowUs - triggeredUs > ECHO_START_TIMEOUT_US) return Reading {Status::NoSensor};
    return std::nullopt;
}

/**
 * Pings the enabled sensors in turn from the sensor task. `Pins` drives the hardware: `trigger(i)` forgets sensor
 * i's captured edges and pulses its trigger pin, `echoHigh(i)` reads its echo pin, and `echo(i)` copies the edges an
 * interrupt captured since the trigger.
 */
template <class Pins>
class Ranger {
  public:
    explicit Ranger(Pins &pins) : _pins(pins) {}

    /** Which sensors to ping; every reading starts over. */
    void enable(std::array<bool, COUNT> enabled) {
        _enabled = enabled;
        _readings = {};
        _sensor = COUNT - 1;
        _pinging = false;
        _started = false;
    }

    /** Settles the ping in flight, and pings the next sensor when its slot begins. Whether a reading changed. */
    bool tick(uint32_t nowUs) {
        bool changed = false;
        if (_pinging) {
            if (const auto reading = evaluate(_triggeredUs, _pins.echo(_sensor), nowUs)) {
                changed = set(_sensor, *reading);
                _pinging = false;
            }
        }
        if (_started && nowUs - _slotStartUs < SLOT_US) return changed;
        const int sensor = nextSensor();
        if (sensor < 0) return changed;
        _started = true;
        _slotStartUs = nowUs;
        _sensor = sensor;
        // Still high from the last ping: the module is waiting out its own timeout, having heard nothing.
        if (_pins.echoHigh(sensor)) return set(sensor, {Status::NoEcho}) || changed;
        _pins.trigger(sensor);
        _triggeredUs = nowUs;
        _pinging = true;
        return changed;
    }

    const std::array<Reading, COUNT> &readings() const { return _readings; }

  private:
    int nextSensor() const {
        for (int step = 1; step <= COUNT; step++) {
            const int sensor = (_sensor + step) % COUNT;
            if (_enabled[sensor]) return sensor;
        }
        return -1;
    }

    bool set(int sensor, const Reading &reading) {
        if (_readings[sensor] == reading) return false;
        _readings[sensor] = reading;
        return true;
    }

    Pins &_pins;
    std::array<bool, COUNT> _enabled {};
    std::array<Reading, COUNT> _readings {};
    int _sensor = COUNT - 1;
    bool _pinging = false;
    bool _started = false;
    uint32_t _triggeredUs = 0;
    uint32_t _slotStartUs = 0;
};

}  // namespace sonar
