#pragma once

#include <peripherals/i2c_bus.h>
#include <peripherals/imu/imu_driver.h>
#include <utils/sleep.h>

/** MPU6050 raw accelerometer and gyro at 200 Hz; fusion happens in Imu, so the DMP is not used. */
class MPU6050Driver final : public ImuDriver {
  public:
    static constexpr uint8_t DEFAULT_ADDR = 0x68;
    static constexpr float ACCEL_LSB_PER_G = 8192.0f;  // +-4 g
    static constexpr float GYRO_LSB_PER_DPS = 65.5f;   // +-500 deg/s

    explicit MPU6050Driver(uint8_t addr = DEFAULT_ADDR) : _addr(addr) {}

    bool begin() override {
        if (!I2CBus::instance().probe(_addr)) return false;
        const uint8_t whoAmI = readReg(REG_WHO_AM_I);
        if (whoAmI != 0x68 && whoAmI != 0x72) return false;
        writeReg(REG_PWR_MGMT_1, 0x80);  // reset
        sleepAtLeastMs(100);
        writeReg(REG_PWR_MGMT_1, 0x01);  // wake, clock from the x gyro's PLL
        writeReg(REG_CONFIG, 0x02);      // low-pass 94 Hz accel, 98 Hz gyro; gyro sampled at 1 kHz
        writeReg(REG_SMPLRT_DIV, 4);     // 1 kHz / (1 + 4) = 200 Hz
        writeReg(REG_GYRO_CONFIG, 0x08);
        writeReg(REG_ACCEL_CONFIG, 0x08);
        writeReg(REG_USER_CTRL, 0x00);   // own I2C master off, which bypass needs
        writeReg(REG_INT_PIN_CFG, 0x02); // bypass: a compass on the auxiliary bus appears on the main bus
        // The gyro reads large spurious rates for its start-up time (30 ms in the datasheet; seen on the robot as
        // the first two reads), which would spoil the gyro bias estimated right after.
        sleepAtLeastMs(50);
        return true;
    }

    bool read(RawImu &raw) override {
        uint8_t b[14];
        if (I2CBus::instance().readReg(_addr, REG_ACCEL_XOUT_H, b, sizeof(b)) != ESP_OK) return false;
        for (int axis = 0; axis < 3; axis++) {
            raw.accel[axis] = word(b, axis * 2) / ACCEL_LSB_PER_G * STANDARD_GRAVITY;
            raw.gyro[axis] = word(b, 8 + axis * 2) / GYRO_LSB_PER_DPS * RAD_PER_DEG;
        }
        raw.temperature = word(b, 6) / 340.0f + 36.53f;
        raw.hasMag = false;
        raw.hasOrientation = false;
        return true;
    }

    const char *name() const override { return "MPU6050"; }
    uint32_t rateHz() const override { return 200; }

  private:
    static constexpr uint8_t REG_SMPLRT_DIV = 0x19;
    static constexpr uint8_t REG_CONFIG = 0x1A;
    static constexpr uint8_t REG_GYRO_CONFIG = 0x1B;
    static constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
    static constexpr uint8_t REG_INT_PIN_CFG = 0x37;
    static constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
    static constexpr uint8_t REG_USER_CTRL = 0x6A;
    static constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
    static constexpr uint8_t REG_WHO_AM_I = 0x75;

    static float word(const uint8_t *b, int at) { return static_cast<int16_t>((b[at] << 8) | b[at + 1]); }

    void writeReg(uint8_t reg, uint8_t value) { I2CBus::instance().writeReg(_addr, reg, &value, 1); }

    uint8_t readReg(uint8_t reg) {
        uint8_t value = 0;
        I2CBus::instance().readReg(_addr, reg, &value, 1);
        return value;
    }

    uint8_t _addr;
};
