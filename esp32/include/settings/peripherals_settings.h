#pragma once

#include <template/state_result.h>
#include <sdkconfig.h>
#include <platform_shared/api.pb.h>
#include <global.h>
#include <peripherals/gpio_pins.h>
#include <settings/imu_settings.h>

// Use proto types directly
using PinConfig = api_PinConfig;
using PeripheralsConfiguration = api_PeripheralSettings;

/** The LED strip in force; a settings file from before the LED settings drives the strip as that firmware did. */
inline api_LedSettings effectiveLedSettings(const PeripheralsConfiguration& settings) {
    return settings.has_ws2812 ? settings.ws2812 : api_LedSettings {true, WS2812_PIN};
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
    return settings;
}

inline void PeripheralsConfiguration_read(const PeripheralsConfiguration& settings, PeripheralsConfiguration& proto) {
    proto = settings;
    proto.has_ws2812 = true;
    proto.ws2812 = effectiveLedSettings(settings);
}

// A save without IMU or LED settings, such as the app's pin editor, keeps those in force.
inline StateUpdateResult PeripheralsConfiguration_update(const PeripheralsConfiguration& proto,
                                                         PeripheralsConfiguration& settings) {
    if (proto.has_imu && !validImuSettings(proto.imu)) return StateUpdateResult::ERROR;
    const api_LedSettings led = proto.has_ws2812 ? proto.ws2812 : effectiveLedSettings(settings);
    const api_ImuSettings imu = proto.has_imu ? proto.imu : effectiveImuSettings(settings);
    settings = proto;
    settings.has_imu = true;
    settings.imu = imu;
    settings.has_ws2812 = true;
    settings.ws2812 = led;
    return StateUpdateResult::CHANGED;
}

/**
 * Whether the strip may be driven: off, or away from the I2C bus on the pin it is driven on already (`drivenPin`, -1
 * for none), which the strip itself holds, or on a free pin the chip can drive.
 */
inline bool usableLedSettings(const api_LedSettings& led, const PeripheralsConfiguration& settings, int32_t drivenPin) {
    if (!led.enabled) return true;
    if (led.pin == settings.sda || led.pin == settings.scl) return false;
    return led.pin == drivenPin || usableOutputPin(led.pin);
}

// A save from the app is refused for a strip it cannot drive; a stored file is loaded as it is, and such a strip is
// left dark, so a file moved to another board does not lose the other settings. The stored strip stands for the one
// driven: a strip that could not be driven at boot stays dark however often it is saved.
inline StateUpdateResult PeripheralsConfiguration_save(const PeripheralsConfiguration& proto,
                                                       PeripheralsConfiguration& settings) {
    const api_LedSettings stored = effectiveLedSettings(settings);
    if (proto.has_ws2812 && !usableLedSettings(proto.ws2812, proto, stored.enabled ? stored.pin : -1))
        return StateUpdateResult::ERROR;
    return PeripheralsConfiguration_update(proto, settings);
}
