#pragma once

#include <peripherals/i2c_bus.h>
#include <algorithm>

class PCA9685Driver {
  public:
    static constexpr uint8_t DEFAULT_ADDR = 0x40;

    PCA9685Driver(uint8_t addr = DEFAULT_ADDR) : _addr(addr) {}

    bool begin() {
        if (!I2CBus::instance().probe(_addr)) return false;

        reset();
        setOscillatorFrequency(25000000);
        setPWMFreq(50);

        _initialized = true;
        return true;
    }

    void reset() {
        writeReg(REG_MODE1, MODE1_RESTART);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    void sleep() {
        uint8_t mode = readReg(REG_MODE1);
        writeReg(REG_MODE1, (mode & ~MODE1_RESTART) | MODE1_SLEEP);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    void wakeup() {
        uint8_t mode = readReg(REG_MODE1);
        uint8_t wakeMode = mode & ~MODE1_SLEEP;
        writeReg(REG_MODE1, wakeMode);
        vTaskDelay(pdMS_TO_TICKS(5));
        writeReg(REG_MODE1, wakeMode | MODE1_RESTART);
    }

    void setPWMFreq(float freq) {
        freq = std::clamp(freq, 1.0f, 3500.0f);
        float prescaleval = ((_oscFreq / (freq * 4096.0f)) + 0.5f) - 1;
        if (prescaleval < 3) prescaleval = 3;
        if (prescaleval > 255) prescaleval = 255;
        uint8_t prescale = static_cast<uint8_t>(prescaleval);

        uint8_t oldMode = readReg(REG_MODE1);
        uint8_t newMode = (oldMode & ~MODE1_RESTART) | MODE1_SLEEP;
        writeReg(REG_MODE1, newMode);
        writeReg(REG_PRESCALE, prescale);
        writeReg(REG_MODE1, oldMode);
        vTaskDelay(pdMS_TO_TICKS(5));
        writeReg(REG_MODE1, oldMode | MODE1_RESTART | MODE1_AI);
    }

    void setOscillatorFrequency(uint32_t freq) { _oscFreq = freq; }

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

    uint8_t _addr;
    bool _initialized = false;
    uint32_t _oscFreq = 25000000;
};
