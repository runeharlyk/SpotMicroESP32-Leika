#pragma once

#include <platform_shared/api.pb.h>
#include <peripherals/power_guard.h>
#include <utils/finite.h>

/** The servo rail's INA219; a settings file from before it gets these. */
inline api_PowerSensorSettings powerSensorDefaults() {
    return {.address = 0x41,
            .shunt_ohms = 0.1f,
            .max_current_a = 3.2f,
            .cutoff_voltage = 0,
            .stall_current_a = 3.0f,
            .stall_time_ms = 1000};
}

inline api_PowerSensorSettings effectivePowerSettings(const api_PeripheralSettings &settings) {
    return settings.has_power_sensor ? settings.power_sensor : powerSensorDefaults();
}

// A hair over the INA219's widest shunt range of 0.32 V, so 3.2 A through 0.1 Ohm, which floats multiply to just
// above it, passes.
constexpr float MAX_SHUNT_VOLTS = 0.3201f;

/**
 * The INA219 takes 0x40 to 0x4F, and 0x40 is the servo board's. At most 0.32 V across the shunt, its widest range; a
 * cut-off within the bus's 26 V rating.
 */
inline bool validPowerSettings(const api_PowerSensorSettings &power) {
    if (power.address < 0x41 || power.address > 0x4F) return false;
    if (!isFinite(power.shunt_ohms) || !isFinite(power.max_current_a) || !isFinite(power.cutoff_voltage) ||
        !isFinite(power.stall_current_a))
        return false;
    if (power.shunt_ohms <= 0 || power.max_current_a <= 0 || power.max_current_a * power.shunt_ohms > MAX_SHUNT_VOLTS)
        return false;
    return power.cutoff_voltage >= 0 && power.cutoff_voltage <= 26 && power.stall_current_a >= 0;
}

inline PowerGuardConfig powerGuardConfigFrom(const api_PowerSensorSettings &power) {
    return {power.cutoff_voltage, power.stall_current_a, power.stall_time_ms};
}
