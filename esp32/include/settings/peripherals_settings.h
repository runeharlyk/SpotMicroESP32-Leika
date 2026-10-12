#pragma once

#include <template/state_result.h>
#include <sdkconfig.h>
#include <platform_shared/api.pb.h>
#include <global.h>
#include <peripherals/gpio_pins.h>
#include <settings/imu_settings.h>
#include <settings/sonar_settings.h>

// Use proto types directly
using PinConfig = api_PinConfig;
using PeripheralsConfiguration = api_PeripheralSettings;

/** The LED strip in force; a settings file from before the LED settings drives the strip as that firmware did. */
inline api_LedSettings effectiveLedSettings(const PeripheralsConfiguration& settings) {
    return settings.has_ws2812 ? settings.ws2812 : api_LedSettings {true, WS2812_PIN};
}

/** The sonars a save leaves in force: those it carries, else the stored ones. */
inline std::array<api_SonarSettings, 2> sonarSettingsAfter(const PeripheralsConfiguration& proto,
                                                           const PeripheralsConfiguration& settings) {
    const std::array<api_SonarSettings, 2> stored = effectiveSonarSettings(settings);
    return {proto.has_left_sonar ? proto.left_sonar : stored[0], proto.has_right_sonar ? proto.right_sonar : stored[1]};
}

inline void setSonarSettings(PeripheralsConfiguration& settings, const std::array<api_SonarSettings, 2>& sonars) {
    settings.has_left_sonar = settings.has_right_sonar = true;
    settings.left_sonar = sonars[0];
    settings.right_sonar = sonars[1];
}

// Default factory settings
inline PeripheralsConfiguration PeripheralsConfiguration_defaults() {
    PeripheralsConfiguration settings = api_PeripheralSettings_init_zero;
    settings.sda = SDA_PIN;
    settings.scl = SCL_PIN;
    settings.frequency = I2C_FREQUENCY;
    settings.pins_count = 0;
    settings.has_imu = true;
    settings.imu = imuSettingsDefaults();
    settings.has_ws2812 = true;
    settings.ws2812 = effectiveLedSettings(settings);
    setSonarSettings(settings, effectiveSonarSettings(settings));
    return settings;
}

inline void PeripheralsConfiguration_read(const PeripheralsConfiguration& settings, PeripheralsConfiguration& proto) {
    proto = settings;
    proto.has_ws2812 = true;
    proto.ws2812 = effectiveLedSettings(settings);
    setSonarSettings(proto, effectiveSonarSettings(settings));
}

// A save without IMU, LED or sonar settings, such as the app's pin editor, keeps those in force.
inline StateUpdateResult PeripheralsConfiguration_update(const PeripheralsConfiguration& proto,
                                                         PeripheralsConfiguration& settings) {
    if (proto.has_imu && !validImuSettings(proto.imu)) return StateUpdateResult::ERROR;
    const api_LedSettings led = proto.has_ws2812 ? proto.ws2812 : effectiveLedSettings(settings);
    const api_ImuSettings imu = proto.has_imu ? proto.imu : effectiveImuSettings(settings);
    const std::array<api_SonarSettings, 2> sonars = sonarSettingsAfter(proto, settings);
    settings = proto;
    settings.has_imu = true;
    settings.imu = imu;
    settings.has_ws2812 = true;
    settings.ws2812 = led;
    setSonarSettings(settings, sonars);
    return StateUpdateResult::CHANGED;
}

/**
 * Whether the strip may be driven: off, or away from the I2C bus and the sonars on the pin it is driven on already
 * (`drivenPin`, -1 for none), which the strip itself holds, or on a free pin the chip can drive.
 */
inline bool usableLedSettings(const api_LedSettings& led, const PeripheralsConfiguration& settings,
                              const std::array<api_SonarSettings, 2>& sonars, int32_t drivenPin) {
    if (!led.enabled) return true;
    if (led.pin == settings.sda || led.pin == settings.scl) return false;
    if (sonarUsesPin(sonars[0], led.pin) || sonarUsesPin(sonars[1], led.pin)) return false;
    return led.pin == drivenPin || usableOutputPin(led.pin);
}

inline bool sonarTriggerPin(int32_t pin) { return usableOutputPin(pin) && !cameraPin(pin); }
inline bool sonarEchoPin(int32_t pin) { return usableInputPin(pin) && !cameraPin(pin); }

/** Why the sonars cannot run beside the I2C bus and the strip `led` on this board, or nullptr. */
inline const char* sonarProblem(const std::array<api_SonarSettings, 2>& sonars, const PeripheralsConfiguration& settings,
                                const api_LedSettings& led) {
    return sonarPinProblem(sonars, {settings.sda, settings.scl, led.enabled ? led.pin : -1},
                           {sonarTriggerPin, sonarEchoPin});
}

// A save from the app is refused for a strip or sonars it cannot drive; a stored file is loaded as it is, and such a
// strip is left dark and such sonars off, so a file moved to another board does not lose the other settings. The
// stored strip stands for the one driven: a strip that could not be driven at boot stays dark however often it is saved.
inline StateUpdateResult PeripheralsConfiguration_save(const PeripheralsConfiguration& proto,
                                                       PeripheralsConfiguration& settings) {
    const api_LedSettings stored = effectiveLedSettings(settings);
    const api_LedSettings led = proto.has_ws2812 ? proto.ws2812 : stored;
    const std::array<api_SonarSettings, 2> sonars = sonarSettingsAfter(proto, settings);
    if (proto.has_ws2812 && !usableLedSettings(proto.ws2812, proto, sonars, stored.enabled ? stored.pin : -1))
        return StateUpdateResult::ERROR;
    if ((proto.has_left_sonar || proto.has_right_sonar) && sonarProblem(sonars, proto, led))
        return StateUpdateResult::ERROR;
    return PeripheralsConfiguration_update(proto, settings);
}
