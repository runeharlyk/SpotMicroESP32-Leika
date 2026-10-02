#pragma once

#include <peripherals/i2c_bus.h>
#include <utils/sleep.h>
#include <algorithm>
#include <cmath>

class PCA9685Driver {
  public:
    static constexpr uint8_t DEFAULT_ADDR = 0x40;

    PCA9685Driver(uint8_t addr = DEFAULT_ADDR) : _addr(addr) {}

    /**
     * Starts the chip asleep with every channel fully off, and leaves it asleep until wakeup(). Across an ESP32 reset
     * the chip keeps running, or sleeps holding the last pose, and waking it restarts the channels it held: the legs
     * would jump to that pose. The prescale can only be written while asleep.
     */
    bool begin(uint32_t oscillatorHz, float pwmHz) {
        if (!I2CBus::instance().probe(_addr)) return false;

        writeReg(REG_ALL_LED_OFF_H, FULL_OFF_BIT);
        writeReg(REG_MODE1, MODE1_SLEEP | MODE1_AI);
        writeReg(REG_PRESCALE, prescale(oscillatorHz, pwmHz));

        _initialized = true;
        return true;
    }

    void sleep() {
        uint8_t mode = readReg(REG_MODE1);
        writeReg(REG_MODE1, (mode & ~MODE1_RESTART) | MODE1_SLEEP);
        sleepAtLeastMs(5);
    }

    void wakeup() {
        uint8_t mode = readReg(REG_MODE1);
        uint8_t wakeMode = mode & ~MODE1_SLEEP;
        writeReg(REG_MODE1, wakeMode);
        sleepAtLeastMs(5);
        writeReg(REG_MODE1, wakeMode | MODE1_RESTART);
    }

    uint8_t setPWM(uint8_t channel, uint16_t on, uint16_t off) {
        if (channel > 15) return 1;

        uint8_t buf[4] = {static_cast<uint8_t>(on & 0xFF), static_cast<uint8_t>(on >> 8),
                          static_cast<uint8_t>(off & 0xFF), static_cast<uint8_t>(off >> 8)};
        return I2CBus::instance().writeReg(_addr, REG_LED0_ON_L + 4 * channel, buf, 4) == ESP_OK ? 0 : 1;
    }

    uint8_t setMultiplePWM(const uint16_t* values, uint8_t length) {
        if (length > 16) length = 16;

        uint8_t buf[64];
        for (uint8_t i = 0; i < length; i++) {
            uint16_t val = values[i] > 4095 ? 4095 : values[i];
            uint8_t* b = &buf[i * 4];
            b[0] = 0;
            if (val == 0) {
                b[1] = 0;
                b[2] = 0;
                b[3] = FULL_OFF_BIT;
            } else if (val == 4095) {
                b[1] = FULL_ON_BIT;
                b[2] = 0;
                b[3] = 0;
            } else {
                b[1] = 0;
                b[2] = val & 0xFF;
                b[3] = val >> 8;
            }
        }
        return I2CBus::instance().writeReg(_addr, REG_LED0_ON_L, buf, length * 4) == ESP_OK ? 0 : 1;
    }

    bool isInitialized() const { return _initialized; }

  private:
    static constexpr uint8_t REG_MODE1 = 0x00;
    static constexpr uint8_t REG_MODE2 = 0x01;
    static constexpr uint8_t REG_PRESCALE = 0xFE;
    static constexpr uint8_t REG_ALL_LED_OFF_H = 0xFD;
    static constexpr uint8_t REG_LED0_ON_L = 0x06;

    static constexpr uint8_t MODE1_RESTART = 0x80;
    static constexpr uint8_t MODE1_SLEEP = 0x10;
    static constexpr uint8_t MODE1_AI = 0x20;

    static constexpr uint8_t FULL_ON_BIT = 0x10;
    static constexpr uint8_t FULL_OFF_BIT = 0x10;

    void writeReg(uint8_t reg, uint8_t val) { I2CBus::instance().writeReg(_addr, reg, &val, 1); }

    uint8_t readReg(uint8_t reg) {
        uint8_t val = 0;
        I2CBus::instance().readReg(_addr, reg, &val, 1);
        return val;
    }

    // The datasheet's prescale: round(oscillator / (4096 * rate)) - 1, within the chip's 3 to 255.
    static uint8_t prescale(uint32_t oscillatorHz, float pwmHz) {
        const float value = std::round(oscillatorHz / (4096.0f * pwmHz)) - 1;
        return static_cast<uint8_t>(std::clamp(value, 3.0f, 255.0f));
    }

    uint8_t _addr;
    bool _initialized = false;
};
