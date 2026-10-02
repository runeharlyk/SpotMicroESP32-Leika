#pragma once

#include <peripherals/i2c_bus.h>
#include <utils/sleep.h>
#include <peripherals/imu/imu_driver.h>

class BNO055Driver final : public ImuDriver {
  public:
    static constexpr uint8_t DEFAULT_ADDR = 0x29;

    explicit BNO055Driver(uint8_t addr = DEFAULT_ADDR) : _addr(addr) {}

    bool begin() override {
        if (!I2CBus::instance().probe(_addr)) return false;

        uint8_t id = readReg(REG_CHIP_ID);
        if (id != BNO055_ID) return false;

        writeReg(REG_OPR_MODE, MODE_CONFIG);
        sleepAtLeastMs(25);

        writeReg(REG_SYS_TRIGGER, 0x20);
        sleepAtLeastMs(650);

        // The datasheet gives 650 ms from reset to ready; one that does not come back within another second is
        // absent or broken, and waiting on it forever would stop the sensors for good.
        for (int attempt = 0; readReg(REG_CHIP_ID) != BNO055_ID; attempt++) {
            if (attempt == 100) return false;
            sleepAtLeastMs(10);
        }
        sleepAtLeastMs(50);

        writeReg(REG_PWR_MODE, PWR_NORMAL);
        sleepAtLeastMs(10);

        writeReg(REG_PAGE_ID, 0);
        writeReg(REG_SYS_TRIGGER, 0x80);
        sleepAtLeastMs(10);

        writeReg(REG_OPR_MODE, MODE_NDOF);
        sleepAtLeastMs(20);

        return true;
    }

    /** Accelerometer, compass, gyro and the chip's own fusion, read in one burst. */
    bool read(RawImu &raw) override {
        uint8_t b[32];  // 0x08 accel, 0x0E mag, 0x14 gyro, 0x1A euler (unused), 0x20 quaternion
        if (I2CBus::instance().readReg(_addr, REG_ACCEL_DATA_X_LSB, b, sizeof(b)) != ESP_OK) return false;
        uint8_t temperature = 0;
        if (I2CBus::instance().readReg(_addr, REG_TEMP, &temperature, 1) != ESP_OK) return false;
        for (int axis = 0; axis < 3; axis++) {
            raw.accel[axis] = word(b, axis * 2) / 100.0f;
            raw.mag[axis] = word(b, 6 + axis * 2) / 16.0f;
            raw.gyro[axis] = word(b, 12 + axis * 2) / 16.0f * RAD_PER_DEG;
        }
        for (int i = 0; i < 4; i++) raw.quat[i] = word(b, 24 + i * 2) / 16384.0f;
        raw.temperature = static_cast<int8_t>(temperature);
        raw.hasMag = true;
        raw.hasOrientation = true;
        return true;
    }

    const char *name() const override { return "BNO055"; }
    uint32_t rateHz() const override { return 100; }
    uint32_t magRateHz() const override { return 20; }

    /** System, gyro, accelerometer and compass calibration, two bits each, 3 meaning calibrated. */
    uint8_t calibrationStatus() { return readReg(REG_CALIB_STAT); }

  private:
    static constexpr uint8_t BNO055_ID = 0xA0;
    static constexpr uint8_t REG_CHIP_ID = 0x00;
    static constexpr uint8_t REG_PAGE_ID = 0x07;
    static constexpr uint8_t REG_ACCEL_DATA_X_LSB = 0x08;
    static constexpr uint8_t REG_TEMP = 0x34;
    static constexpr uint8_t REG_OPR_MODE = 0x3D;
    static constexpr uint8_t REG_PWR_MODE = 0x3E;
    static constexpr uint8_t REG_SYS_TRIGGER = 0x3F;
    static constexpr uint8_t REG_CALIB_STAT = 0x35;

    static constexpr uint8_t MODE_CONFIG = 0x00;
    static constexpr uint8_t MODE_NDOF = 0x0C;
    static constexpr uint8_t PWR_NORMAL = 0x00;

    void writeReg(uint8_t reg, uint8_t val) { I2CBus::instance().writeReg(_addr, reg, &val, 1); }

    uint8_t readReg(uint8_t reg) {
        uint8_t val = 0;
        I2CBus::instance().readReg(_addr, reg, &val, 1);
        return val;
    }

    static float word(const uint8_t *b, int at) { return static_cast<int16_t>(b[at] | (b[at + 1] << 8)); }

    uint8_t _addr;
};
