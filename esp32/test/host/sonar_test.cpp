// Host test of the HC-SR04 ranging in peripherals/sonar_ranging.h and of the sonar settings in
// settings/peripherals_settings.h, against scripted pins and a scripted chip.
#include <cmath>
#include <cstdio>
#include <vector>
#include <peripherals/sonar_ranging.h>
#include <settings/peripherals_settings.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static bool near(float a, float b, float tolerance) { return std::fabs(a - b) < tolerance; }

using sonar::Echo;
using sonar::Reading;
using sonar::Status;

static Echo pulse(uint32_t riseUs, uint32_t widthUs) { return {true, true, riseUs, riseUs + widthUs}; }

static void testDistance() {
    // 343 m/s there and back: 1 m is 5831 us of echo, the HC-SR04's 2 cm minimum 117 us.
    CHECK(near(sonar::distanceCm(5831), 100.0f, 0.1f));
    CHECK(near(sonar::distanceCm(117), 2.0f, 0.01f));
    CHECK(near(sonar::distanceCm(23324), 400.0f, 0.5f));
}

static void testEvaluate() {
    const uint32_t t0 = 1000;
    // Nothing yet, or an echo still high inside the timeout: the ping is still out.
    CHECK(!sonar::evaluate(t0, {}, t0 + 4000));
    CHECK(!sonar::evaluate(t0, {true, false, t0 + 500, 0}, t0 + 500 + sonar::ECHO_TIMEOUT_US));

    const auto metre = sonar::evaluate(t0, pulse(t0 + 450, 5831), t0 + 7000);
    CHECK(metre && metre->status == Status::Distance && near(metre->distanceCm, 100.0f, 0.1f));

    // The longest echo still a distance, and one a microsecond longer.
    const auto edge = sonar::evaluate(t0, pulse(t0 + 450, sonar::ECHO_TIMEOUT_US), t0 + 30000);
    CHECK(edge && edge->status == Status::Distance && near(edge->distanceCm, 428.75f, 0.1f));
    const auto past = sonar::evaluate(t0, pulse(t0 + 450, sonar::ECHO_TIMEOUT_US + 1), t0 + 30000);
    CHECK(past && past->status == Status::NoEcho && past->distanceCm == 0);

    // A module that heard nothing holds its echo for some 38 ms: no echo, whether it has fallen yet or not.
    const auto held = sonar::evaluate(t0, pulse(t0 + 450, 38000), t0 + 40000);
    CHECK(held && held->status == Status::NoEcho);
    const auto stillHigh = sonar::evaluate(t0, {true, false, t0 + 450, 0}, t0 + 450 + sonar::ECHO_TIMEOUT_US + 1);
    CHECK(stillHigh && stillHigh->status == Status::NoEcho);

    // An echo that never starts: no sensor, once the start timeout has passed and not before.
    CHECK(!sonar::evaluate(t0, {}, t0 + sonar::ECHO_START_TIMEOUT_US));
    const auto absent = sonar::evaluate(t0, {}, t0 + sonar::ECHO_START_TIMEOUT_US + 1);
    CHECK(absent && absent->status == Status::NoSensor);

    // Across the 32-bit wrap of the microsecond clock, 71 minutes after boot.
    const uint32_t late = 0xFFFFF000u;
    const auto wrapped = sonar::evaluate(late, pulse(late + 450, 5831), late + 7000);
    CHECK(wrapped && wrapped->status == Status::Distance && near(wrapped->distanceCm, 100.0f, 0.1f));
    CHECK(!sonar::evaluate(late, {}, late + 4000));
}

// Each sensor answers a trigger with a scripted echo: `delayUs` after it, `widthUs` long; no sensor never answers.
class ScriptedPins {
  public:
    struct Sensor {
        bool present = true;
        uint32_t delayUs = 450, widthUs = 5831;
        bool stuckHigh = false;
    };
    std::array<Sensor, sonar::COUNT> sensors {};
    struct Trigger {
        int sensor;
        uint32_t atUs;
    };
    std::vector<Trigger> triggers;
    uint32_t nowUs = 0;

    void trigger(int sensor) {
        triggers.push_back({sensor, nowUs});
        _triggeredUs[sensor] = nowUs;
    }
    bool echoHigh(int sensor) const { return sensors[sensor].stuckHigh; }
    Echo echo(int sensor) const {
        const Sensor &s = sensors[sensor];
        if (!s.present) return {};
        const uint32_t rise = _triggeredUs[sensor] + s.delayUs, fall = rise + s.widthUs;
        Echo echo;
        if (nowUs - _triggeredUs[sensor] >= s.delayUs) echo = {true, false, rise, 0};
        if (nowUs - _triggeredUs[sensor] >= s.delayUs + s.widthUs) echo = {true, true, rise, fall};
        return echo;
    }

  private:
    std::array<uint32_t, sonar::COUNT> _triggeredUs {};
};

// Ticks the ranger every 5 ms, as the sensor task does, for `durationUs`.
static void run(sonar::Ranger<ScriptedPins> &ranger, ScriptedPins &pins, uint32_t durationUs) {
    for (const uint32_t end = pins.nowUs + durationUs; pins.nowUs != end; pins.nowUs += 5000) ranger.tick(pins.nowUs);
}

static void testAlternation() {
    ScriptedPins pins;
    pins.nowUs = 0xFFFF0000u;  // across the clock's wrap too
    pins.sensors[0].widthUs = 5831;   // 1 m
    pins.sensors[1].widthUs = 11662;  // 2 m
    sonar::Ranger<ScriptedPins> ranger(pins);
    ranger.enable({true, true});
    run(ranger, pins, 1000000);

    // Ten pings a second each, taking turns, a whole slot apart: one sensor's burst has died away before the other's.
    CHECK(pins.triggers.size() == 20);
    for (size_t i = 0; i < pins.triggers.size(); i++) {
        CHECK(pins.triggers[i].sensor == static_cast<int>(i % 2));
        if (i > 0) CHECK(pins.triggers[i].atUs - pins.triggers[i - 1].atUs >= sonar::SLOT_US);
    }
    const auto &readings = ranger.readings();
    CHECK(readings[0].status == Status::Distance && near(readings[0].distanceCm, 100.0f, 0.1f));
    CHECK(readings[1].status == Status::Distance && near(readings[1].distanceCm, 200.0f, 0.1f));
}

static void testOneSensor() {
    ScriptedPins pins;
    sonar::Ranger<ScriptedPins> ranger(pins);
    ranger.enable({false, true});
    run(ranger, pins, 1000000);
    // The one sensor gets every slot; the other is never touched and reads off.
    CHECK(pins.triggers.size() == 20);
    for (const auto &trigger : pins.triggers) CHECK(trigger.sensor == 1);
    CHECK(ranger.readings()[0].status == Status::Off);
    CHECK(ranger.readings()[1].status == Status::Distance);

    ranger.enable({false, false});
    pins.triggers.clear();
    run(ranger, pins, 200000);
    CHECK(pins.triggers.empty());
    CHECK(ranger.readings()[1].status == Status::Off);
}

static void testTimeouts() {
    ScriptedPins pins;
    pins.sensors[0].widthUs = 38000;    // heard nothing: held its echo
    pins.sensors[1].present = false;    // unplugged
    sonar::Ranger<ScriptedPins> ranger(pins);
    ranger.enable({true, true});
    run(ranger, pins, 500000);
    CHECK(ranger.readings()[0].status == Status::NoEcho);
    CHECK(ranger.readings()[1].status == Status::NoSensor);
    // A missing or silent sensor does not stall the other: both still get their turns.
    CHECK(pins.triggers.size() == 10);

    // A sensor still holding its echo from before is not triggered again, and has heard nothing.
    ScriptedPins stuck;
    stuck.sensors[0].stuckHigh = true;
    sonar::Ranger<ScriptedPins> stuckRanger(stuck);
    stuckRanger.enable({true, true});
    run(stuckRanger, stuck, 500000);
    for (const auto &trigger : stuck.triggers) CHECK(trigger.sensor == 1);
    CHECK(stuck.triggers.size() == 5);
    CHECK(stuckRanger.readings()[0].status == Status::NoEcho);
    CHECK(stuckRanger.readings()[1].status == Status::Distance);
}

static void testTickReportsChanges() {
    ScriptedPins pins;
    pins.sensors[1].present = false;
    sonar::Ranger<ScriptedPins> ranger(pins);
    ranger.enable({true, true});
    int changes = 0;
    for (; pins.nowUs < 1000000; pins.nowUs += 5000) changes += ranger.tick(pins.nowUs);
    // The first distance and the first no-sensor; the same readings again are no change.
    CHECK(changes == 2);
    pins.sensors[0].widthUs = 11662;
    for (const uint32_t end = pins.nowUs + 200000; pins.nowUs < end; pins.nowUs += 5000) changes += ranger.tick(pins.nowUs);
    CHECK(changes == 3);
    CHECK(near(ranger.readings()[0].distanceCm, 200.0f, 0.1f));
}

static api_SonarSettings sonarOn(int32_t trigger, int32_t echo) { return {true, trigger, echo}; }

static bool anyOutput(int32_t) { return true; }
static bool anyInput(int32_t) { return true; }
static bool outputBelow34(int32_t pin) { return pin < 34; }

static void testPinProblems() {
    const HeldPins held {.sda = 8, .scl = 9, .led = 48};
    const GpioCheck chip {anyOutput, anyInput};
    using Sonars = std::array<api_SonarSettings, 2>;
    const api_SonarSettings none {};

    CHECK(sonarPinProblem(Sonars {sonarOn(1, 2), sonarOn(3, 4)}, held, chip) == nullptr);
    CHECK(sonarPinProblem(Sonars {}, held, chip) == nullptr);

    CHECK(sonarPinProblem(Sonars {sonarOn(8, 2), none}, held, chip) != nullptr);   // trigger on SDA
    CHECK(sonarPinProblem(Sonars {none, sonarOn(1, 9)}, held, chip) != nullptr);   // echo on SCL
    CHECK(sonarPinProblem(Sonars {sonarOn(48, 2), none}, held, chip) != nullptr);  // on the strip
    CHECK(sonarPinProblem(Sonars {sonarOn(48, 2), none}, {8, 9, -1}, chip) == nullptr);  // no strip there
    CHECK(sonarPinProblem(Sonars {sonarOn(5, 5), none}, held, chip) != nullptr);   // trigger and echo one pin
    CHECK(sonarPinProblem(Sonars {sonarOn(1, 2), sonarOn(3, 1)}, held, chip) != nullptr);  // the sonars share one
    CHECK(sonarPinProblem(Sonars {sonarOn(1, 2), sonarOn(2, 4)}, held, chip) != nullptr);

    // A sonar that is off holds nothing, whatever pins it names.
    CHECK(sonarPinProblem(Sonars {api_SonarSettings {false, 8, 48}, sonarOn(1, 2)}, held, chip) == nullptr);
    CHECK(sonarPinProblem(Sonars {api_SonarSettings {false, 1, 2}, sonarOn(1, 2)}, held, chip) == nullptr);

    // An input-only pin serves an echo but not a trigger.
    const GpioCheck inputOnlyAbove33 {outputBelow34, anyInput};
    CHECK(sonarPinProblem(Sonars {sonarOn(1, 34), none}, held, inputOnlyAbove33) == nullptr);
    CHECK(sonarPinProblem(Sonars {sonarOn(34, 1), none}, held, inputOnlyAbove33) != nullptr);
}

static PeripheralsConfiguration board() {
    PeripheralsConfiguration settings = PeripheralsConfiguration_defaults();
    settings.sda = 47;
    settings.scl = 21;
    settings.ws2812 = {true, 48};
    return settings;
}

static void testSettings() {
    gpio_stub::reserved = 0;
    gpio_stub::inputOnly = 0;

    // A file from before the sonars has them off, and reads back with them off.
    PeripheralsConfiguration old = board();
    old.has_left_sonar = old.has_right_sonar = false;
    PeripheralsConfiguration read {};
    PeripheralsConfiguration_read(old, read);
    CHECK(read.has_left_sonar && !read.left_sonar.enabled && read.has_right_sonar && !read.right_sonar.enabled);

    PeripheralsConfiguration stored = board();
    PeripheralsConfiguration proto = stored;
    proto.left_sonar = sonarOn(1, 2);
    proto.right_sonar = sonarOn(3, 4);
    CHECK(PeripheralsConfiguration_save(proto, stored) == StateUpdateResult::CHANGED);
    CHECK(stored.left_sonar.enabled && stored.left_sonar.trigger_pin == 1 && stored.left_sonar.echo_pin == 2);
    CHECK(stored.right_sonar.enabled && stored.right_sonar.trigger_pin == 3 && stored.right_sonar.echo_pin == 4);

    // A save without them, such as the I2C page's, keeps them.
    PeripheralsConfiguration pinsOnly = stored;
    // As decoded: a message left out reads as zeros.
    pinsOnly.has_left_sonar = pinsOnly.has_right_sonar = false;
    pinsOnly.left_sonar = pinsOnly.right_sonar = {};
    pinsOnly.has_ws2812 = false;
    pinsOnly.ws2812 = {};
    CHECK(PeripheralsConfiguration_save(pinsOnly, stored) == StateUpdateResult::CHANGED);
    CHECK(stored.left_sonar.enabled && stored.right_sonar.trigger_pin == 3);

    // Refused, and nothing changed: on the strip, on I2C, on a pin a driver reserved, on a pin that does not exist.
    const auto refused = [&stored](api_SonarSettings left) {
        PeripheralsConfiguration attempt = stored;
        attempt.left_sonar = left;
        const PeripheralsConfiguration before = stored;
        const bool error = PeripheralsConfiguration_save(attempt, stored) == StateUpdateResult::ERROR;
        return error && stored.left_sonar.trigger_pin == before.left_sonar.trigger_pin &&
               stored.left_sonar.echo_pin == before.left_sonar.echo_pin;
    };
    CHECK(refused(sonarOn(48, 2)));
    CHECK(refused(sonarOn(1, 47)));
    CHECK(refused(sonarOn(23, 2)));  // no GPIO 23 on an ESP32-S3
    gpio_stub::reserved = BIT64(5);
    CHECK(refused(sonarOn(5, 2)));
    CHECK(refused(sonarOn(1, 5)));
    gpio_stub::reserved = 0;
    gpio_stub::inputOnly = BIT64(6);
    CHECK(refused(sonarOn(6, 2)));
    PeripheralsConfiguration echoOnInputOnly = stored;
    echoOnInputOnly.left_sonar = sonarOn(1, 6);
    CHECK(PeripheralsConfiguration_save(echoOnInputOnly, stored) == StateUpdateResult::CHANGED);
    gpio_stub::inputOnly = 0;

    // The strip cannot move onto a sonar's pin, by a save that carries the strip but not the sonars.
    PeripheralsConfiguration ledOntoSonar = stored;
    ledOntoSonar.has_left_sonar = ledOntoSonar.has_right_sonar = false;
    ledOntoSonar.left_sonar = ledOntoSonar.right_sonar = {};
    ledOntoSonar.ws2812 = {true, 3};
    CHECK(PeripheralsConfiguration_save(ledOntoSonar, stored) == StateUpdateResult::ERROR);
    CHECK(stored.ws2812.pin == 48);
    // Nor a sonar onto the strip's pin by a save that carries the sonars but not the strip.
    PeripheralsConfiguration sonarOntoLed = stored;
    sonarOntoLed.has_ws2812 = false;
    sonarOntoLed.ws2812 = {};
    sonarOntoLed.right_sonar = sonarOn(48, 4);
    CHECK(PeripheralsConfiguration_save(sonarOntoLed, stored) == StateUpdateResult::ERROR);
    CHECK(stored.right_sonar.trigger_pin == 3);
    // With the strip off, its pin is free for a sonar.
    PeripheralsConfiguration stripOff = stored;
    stripOff.ws2812 = {false, 48};
    stripOff.right_sonar = sonarOn(48, 4);
    CHECK(PeripheralsConfiguration_save(stripOff, stored) == StateUpdateResult::CHANGED);
    CHECK(stored.right_sonar.trigger_pin == 48);

    // Sonars switched off may name any pins, so a robot without them saves whatever the form held.
    PeripheralsConfiguration off = stored;
    off.ws2812 = {true, 48};
    off.left_sonar = {false, 48, 47};
    off.right_sonar = {false, 48, 48};
    CHECK(PeripheralsConfiguration_save(off, stored) == StateUpdateResult::CHANGED);
}

int main() {
    testDistance();
    testEvaluate();
    testAlternation();
    testOneSensor();
    testTimeouts();
    testTickReportsChanges();
    testPinProblems();
    testSettings();
    if (failures == 0) std::printf("sonar_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
