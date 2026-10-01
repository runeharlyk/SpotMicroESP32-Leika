#pragma once

#include <template/state_result.h>
#include <sdkconfig.h>
#include <platform_shared/api.pb.h>
#include <global.h>
#include <settings/imu_settings.h>

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
    settings.has_imu = true;
    settings.imu = imuSettingsDefaults();
    return settings;
}

// Proto read/update are identity functions since type is the same
inline void PeripheralsConfiguration_read(const PeripheralsConfiguration& settings, PeripheralsConfiguration& proto) {
    proto = settings;
}

// A save without IMU settings, such as the app's pin editor, keeps the IMU settings in force.
inline StateUpdateResult PeripheralsConfiguration_update(const PeripheralsConfiguration& proto,
                                                         PeripheralsConfiguration& settings) {
    if (proto.has_imu && !validImuSettings(proto.imu)) return StateUpdateResult::ERROR;
    const api_ImuSettings imu = proto.has_imu ? proto.imu : effectiveImuSettings(settings);
    settings = proto;
    settings.has_imu = true;
    settings.imu = imu;
    return StateUpdateResult::CHANGED;
}