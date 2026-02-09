#pragma once

#include <sdkconfig.h>
#include <platform_shared/api.pb.h>
#include <global.h>

using PinConfig = api_PinConfig;
using PeripheralsConfiguration = api_PeripheralSettings;

inline PeripheralsConfiguration PeripheralsConfiguration_defaults() {
    PeripheralsConfiguration settings = api_PeripheralSettings_init_zero;
    settings.sda = SDA_PIN;
    settings.scl = SCL_PIN;
    settings.frequency = I2C_FREQUENCY;
    settings.pins_count = 0;
    return settings;
}
