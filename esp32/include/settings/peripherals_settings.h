#pragma once

#include <template/state_result.h>
#include <platform_shared/message.pb.h>

#ifndef SDA_PIN
#define SDA_PIN SDA
#endif
#ifndef SCL_PIN
#define SCL_PIN SCL
#endif
#ifndef I2C_FREQUENCY
#define I2C_FREQUENCY 1000000UL
#endif

class PeripheralsConfiguration {
  public:
    int sda = SDA_PIN;
    int scl = SCL_PIN;
    long frequency = I2C_FREQUENCY;

    static void read(const PeripheralsConfiguration& settings, socket_message_PeripheralSettingsData& proto) {
        proto.sda = settings.sda;
        proto.scl = settings.scl;
        proto.frequency = settings.frequency;
    }

    static StateUpdateResult update(const socket_message_PeripheralSettingsData& proto,
                                    PeripheralsConfiguration& settings) {
        settings.sda = proto.sda > 0 ? proto.sda : SDA_PIN;
        settings.scl = proto.scl > 0 ? proto.scl : SCL_PIN;
        settings.frequency = proto.frequency > 0 ? proto.frequency : I2C_FREQUENCY;
        return StateUpdateResult::CHANGED;
    };
};
