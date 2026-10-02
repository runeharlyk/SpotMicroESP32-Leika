#pragma once

#include <esp_log.h>
#include <peripherals/i2c_bus.h>
#include <peripherals/imu/imu_driver.h>
#include <utils/sleep.h>

/** ICM-20948 accelerometer and gyro, with its AK09916 compass reached through bypass. */
class ICM20948Driver final : public ImuDriver {
  public:
    static constexpr uint8_t ADDRESSES[] = {0x68, 0x69};  // AD0 low or high; breakouts differ
    static constexpr uint8_t AK09916_ADDR = 0x0C;
    static constexpr float ACCEL_LSB_PER_G = 8192.0f;
    static constexpr float GYRO_LSB_PER_DPS = 65.5f;
    static constexpr float MICROTESLA_PER_LSB = 0.15f;

    bool begin() override {
        if (!findChip()) return false;
        writeReg(_addr, REG_PWR_MGMT_1, 0x80);  // reset
        sleepAtLeastMs(100);
        selectBank(0);
        writeReg(_addr, REG_PWR_MGMT_1, 0x01);  // awake, best available clock
        writeReg(_addr, REG_PWR_MGMT_2, 0x00);  // accelerometer and gyro on
        selectBank(2);
        writeReg(_addr, REG_GYRO_SMPLRT_DIV, 4);       // 1.1 kHz / 5 = 220 Hz
        writeReg(_addr, REG_GYRO_CONFIG_1, 0x1B);      // low-pass config 3, +-500 deg/s, low-pass on
        writeReg(_addr, REG_ACCEL_SMPLRT_DIV_1, 0);
        writeReg(_addr, REG_ACCEL_SMPLRT_DIV_2, 4);    // 1.125 kHz / 5 = 225 Hz
        writeReg(_addr, REG_ACCEL_CONFIG, 0x1B);       // low-pass config 3, +-4 g, low-pass on
        selectBank(0);
        writeReg(_addr, REG_USER_CTRL, 0x00);          // own I2C master off, which bypass needs
        writeReg(_addr, REG_INT_PIN_CFG, 0x02);        // bypass: the AK09916 appears on the main bus
        sleepAtLeastMs(50);                            // past the gyro's start-up time, as on the MPU6050
        _hasCompass = readReg(AK09916_ADDR, AK_WIA2) == 0x09;
        if (!_hasCompass) {
            ESP_LOGW("ICM20948", "The AK09916 compass did not answer; running without it");
            return true;
        }
        writeReg(AK09916_ADDR, AK_CNTL3, 0x01);        // soft reset
        sleepAtLeastMs(10);
        writeReg(AK09916_ADDR, AK_CNTL2, 0x08);        // continuous measurement mode 4, 100 Hz
        return true;
    }

    bool read(RawImu &raw) override {
        uint8_t b[14];  // accel x, y, z; gyro x, y, z; temperature
        if (I2CBus::instance().readReg(_addr, REG_ACCEL_XOUT_H, b, sizeof(b)) != ESP_OK) return false;
        for (int axis = 0; axis < 3; axis++) {
            raw.accel[axis] = bigEndian(b, axis * 2) / ACCEL_LSB_PER_G * STANDARD_GRAVITY;
            raw.gyro[axis] = bigEndian(b, 6 + axis * 2) / GYRO_LSB_PER_DPS * RAD_PER_DEG;
        }
        raw.temperature = bigEndian(b, 12) / 333.87f + 21.0f;
        raw.hasOrientation = false;
        raw.hasMag = _hasCompass && readCompass(raw.mag);
        return true;
    }

    const char *name() const override { return "ICM-20948"; }
    uint32_t rateHz() const override { return 200; }
    uint32_t magRateHz() const override { return _hasCompass ? 100 : 0; }

  private:
    static constexpr uint8_t REG_WHO_AM_I = 0x00;
    static constexpr uint8_t REG_USER_CTRL = 0x03;
    static constexpr uint8_t REG_PWR_MGMT_1 = 0x06;
    static constexpr uint8_t REG_PWR_MGMT_2 = 0x07;
    static constexpr uint8_t REG_INT_PIN_CFG = 0x0F;
    static constexpr uint8_t REG_ACCEL_XOUT_H = 0x2D;
    static constexpr uint8_t REG_BANK_SEL = 0x7F;
    static constexpr uint8_t REG_GYRO_SMPLRT_DIV = 0x00;
    static constexpr uint8_t REG_GYRO_CONFIG_1 = 0x01;
    static constexpr uint8_t REG_ACCEL_SMPLRT_DIV_1 = 0x10;
    static constexpr uint8_t REG_ACCEL_SMPLRT_DIV_2 = 0x11;
    static constexpr uint8_t REG_ACCEL_CONFIG = 0x14;
    static constexpr uint8_t AK_WIA2 = 0x01;
    static constexpr uint8_t AK_ST1 = 0x10;
    static constexpr uint8_t AK_HXL = 0x11;
    static constexpr uint8_t AK_CNTL2 = 0x31;
    static constexpr uint8_t AK_CNTL3 = 0x32;
    bool findChip() {
        for (uint8_t addr : ADDRESSES) {
            _addr = addr;
            if (!I2CBus::instance().probe(addr)) continue;
            selectBank(0);
            if (readReg(addr, REG_WHO_AM_I) == 0xEA) return true;
        }
        return false;
    }

    // Unverified on hardware: the compass's x matches the accelerometer's, its y and z point the other way.
    static constexpr Vec3 AK_TO_ACCEL = {1, -1, -1};

    // Reading through ST2 ends the measurement, so the next one can latch; HOFL in ST2 marks an overflow.
    bool readCompass(Vec3 &microtesla) {
        uint8_t status = 0;
        if (I2CBus::instance().readReg(AK09916_ADDR, AK_ST1, &status, 1) != ESP_OK || !(status & 0x01)) return false;
        uint8_t b[8];  // x, y, z little-endian, a dummy byte, ST2
        if (I2CBus::instance().readReg(AK09916_ADDR, AK_HXL, b, sizeof(b)) != ESP_OK) return false;
        if (b[7] & 0x08) return false;
        for (int axis = 0; axis < 3; axis++)
            microtesla[axis] = static_cast<int16_t>(b[axis * 2] | (b[axis * 2 + 1] << 8)) * MICROTESLA_PER_LSB *
                               AK_TO_ACCEL[axis];
        return true;
    }

    static float bigEndian(const uint8_t *b, int at) { return static_cast<int16_t>((b[at] << 8) | b[at + 1]); }

    void selectBank(uint8_t bank) { writeReg(_addr, REG_BANK_SEL, static_cast<uint8_t>(bank << 4)); }

    static void writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
        I2CBus::instance().writeReg(addr, reg, &value, 1);
    }

    static uint8_t readReg(uint8_t addr, uint8_t reg) {
        uint8_t value = 0;
        I2CBus::instance().readReg(addr, reg, &value, 1);
        return value;
    }

    uint8_t _addr = ADDRESSES[0];
    bool _hasCompass = false;
};
