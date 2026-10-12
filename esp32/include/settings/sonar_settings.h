#pragma once

#include <array>
#include <cstdint>
#include <platform_shared/api.pb.h>

/** The left and right sonar, in the order the ranging pings them; a settings file from before them has them off. */
inline std::array<api_SonarSettings, 2> effectiveSonarSettings(const api_PeripheralSettings &settings) {
    constexpr api_SonarSettings off {false, -1, -1};
    return {settings.has_left_sonar ? settings.left_sonar : off, settings.has_right_sonar ? settings.right_sonar : off};
}

inline bool sonarUsesPin(const api_SonarSettings &sonar, int32_t pin) {
    return sonar.enabled && (sonar.trigger_pin == pin || sonar.echo_pin == pin);
}

/** The chip's say on a GPIO: whether it can drive it, or read it, with nothing else holding it. */
struct GpioCheck {
    bool (*output)(int32_t pin);
    bool (*input)(int32_t pin);
};

/** The pins the rest of the robot holds, which the chip itself does not know of. */
struct HeldPins {
    int32_t sda, scl;
    int32_t led;  // -1 for no strip
};

/**
 * Why the sonars cannot run on their pins, or nullptr: each enabled one needs a trigger the chip can drive and an echo
 * it can read, apart from each other, from the other sonar, from the I2C bus and from the LED strip.
 */
inline const char *sonarPinProblem(const std::array<api_SonarSettings, 2> &sonars, const HeldPins &held,
                                   const GpioCheck &chip) {
    for (size_t i = 0; i < sonars.size(); i++) {
        const api_SonarSettings &sonar = sonars[i];
        if (!sonar.enabled) continue;
        if (sonar.trigger_pin == sonar.echo_pin) return "a sonar's trigger and echo share a pin";
        for (const int32_t pin : {sonar.trigger_pin, sonar.echo_pin}) {
            if (pin == held.sda || pin == held.scl) return "a sonar is on an I2C pin";
            if (pin == held.led) return "a sonar is on the LED strip's pin";
            if (sonarUsesPin(sonars[1 - i], pin)) return "the two sonars share a pin";
        }
        if (!chip.output(sonar.trigger_pin)) return "a sonar's trigger is on a pin the chip cannot drive or has taken";
        if (!chip.input(sonar.echo_pin)) return "a sonar's echo is on a pin the chip cannot read or has taken";
    }
    return nullptr;
}
