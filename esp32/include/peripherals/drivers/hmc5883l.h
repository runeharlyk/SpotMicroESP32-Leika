#pragma once

#include <peripherals/i2c_bus.h>
#include <peripherals/imu/imu_driver.h>

/** HMC5883L compass, continuous at 75 Hz, in microtesla in its own axes. */
class HMC5883LDriver final : public MagDriver {
  public:
    static constexpr uint8_t DEFAULT_ADDR = 0x1E;
    static constexpr float MICROTESLA_PER_LSB = 100.0f / 1090.0f;  // gain 1090 LSB per gauss

    explicit HMC5883LDriver(uint8_t addr = DEFAULT_ADDR) : _addr(addr) {}

    bool begin() override {
        if (!I2CBus::instance().probe(_addr)) return false;
        if (readReg(REG_ID_A) != 'H' || readReg(REG_ID_B) != '4' || readReg(REG_ID_C) != '3') return false;
        writeReg(REG_CONFIG_A, 0x78);  // 8-sample average, 75 Hz
        writeReg(REG_CONFIG_B, 0x20);  // +-1.3 gauss, 1090 LSB per gauss
        writeReg(REG_MODE, 0x00);      // continuous
        return true;
    }

    bool read(Vec3 &microtesla) override {
        uint8_t b[6];
        if (I2CBus::instance().readReg(_addr, REG_DATA_X_MSB, b, sizeof(b)) != ESP_OK) return false;
        const int16_t x = (b[0] << 8) | b[1], z = (b[2] << 8) | b[3], y = (b[4] << 8) | b[5];
        if (x == OVERFLOW_MARKER || y == OVERFLOW_MARKER || z == OVERFLOW_MARKER) return false;
        microtesla = {x * MICROTESLA_PER_LSB, y * MICROTESLA_PER_LSB, z * MICROTESLA_PER_LSB};
        return true;
    }

    const char *name() const override { return "HMC5883L"; }
    uint32_t rateHz() const override { return 75; }

  private:
    static constexpr uint8_t REG_CONFIG_A = 0x00;
    static constexpr uint8_t REG_CONFIG_B = 0x01;
    static constexpr uint8_t REG_MODE = 0x02;
    static constexpr uint8_t REG_DATA_X_MSB = 0x03;
    static constexpr uint8_t REG_ID_A = 0x0A;
    static constexpr uint8_t REG_ID_B = 0x0B;
    static constexpr uint8_t REG_ID_C = 0x0C;
    static constexpr int16_t OVERFLOW_MARKER = -4096;

    void writeReg(uint8_t reg, uint8_t value) { I2CBus::instance().writeReg(_addr, reg, &value, 1); }

    uint8_t readReg(uint8_t reg) {
        uint8_t value = 0;
        I2CBus::instance().readReg(_addr, reg, &value, 1);
        return value;
    }

    uint8_t _addr;
};
