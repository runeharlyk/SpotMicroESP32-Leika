#pragma once

#include <template/state_result.h>
#include <sdkconfig.h>
#include <platform_shared/api.pb.h>
#include <global.h>

// Use proto types directly
using PinConfig = api_PinConfig;
using PeripheralsConfiguration = api_PeripheralSettings;

// Default factory settings
inline PeripheralsConfiguration PeripheralsConfiguration_defaults() {
    PeripheralsConfiguration settings = api_PeripheralSettings_init_zero;
    settings.sda = SDA_PIN;
    settings.scl = SCL_PIN;
    settings.frequency = I2C_FREQUENCY;
    settings.pins_count = 0;
    return settings;
}

// Proto read/update are identity functions since type is the same
inline void PeripheralsConfiguration_read(const PeripheralsConfiguration& settings, PeripheralsConfiguration& proto) {
    proto = settings;
}

inline StateUpdateResult PeripheralsConfiguration_update(const PeripheralsConfiguration& proto,
                                                         PeripheralsConfiguration& settings) {
    settings = proto;
    return StateUpdateResult::CHANGED;
}