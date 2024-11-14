#pragma once

#include <vector>
#include <ArduinoJson.h>
#include <template/state_result.h>

/*
 * I2C software connection
 */
#ifndef SDA_PIN
#define SDA_PIN SDA
#endif
#ifndef SCL_PIN
#define SCL_PIN SCL
#endif
#ifndef I2C_FREQUENCY
#define I2C_FREQUENCY 100000UL
#endif

class PinConfig {
  public:
    int pin;
    String mode;
    String type;
    String role;

    PinConfig(int p, String m, String t, String r) : pin(p), mode(m), type(t), role(r) {}
};

class PeripheralsConfiguration {
  public:
    int sda = SDA_PIN;
    int scl = SCL_PIN;
    long frequency = I2C_FREQUENCY;
    std::vector<PinConfig> pins;

    static void read(PeripheralsConfiguration &settings, JsonObject &root) {
        root["sda"] = settings.sda;
        root["scl"] = settings.scl;
        root["frequency"] = settings.frequency;
    }

    static StateUpdateResult update(JsonObject &root, PeripheralsConfiguration &settings) {
        settings.sda = root["sda"] | SDA_PIN;
        settings.scl = root["scl"] | SCL_PIN;
        settings.frequency = root["frequency"] | I2C_FREQUENCY;
        return StateUpdateResult::CHANGED;
    };
};