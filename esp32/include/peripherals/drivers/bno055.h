#pragma once

#include <peripherals/i2c_bus.h>

class BNO055Driver {
  public:
    static constexpr uint8_t DEFAULT_ADDR = 0x29;

    BNO055Driver(uint8_t addr = DEFAULT_ADDR) : _addr(addr) {}

    bool begin() {
        if (!I2CBus::instance().probe(_addr)) return false;

        uint8_t id = readReg(REG_CHIP_ID);
        if (id != BNO055_ID) return false;

        writeReg(REG_OPR_MODE, MODE_CONFIG);
        vTaskDelay(pdMS_TO_TICKS(25));

        writeReg(REG_SYS_TRIGGER, 0x20);
        vTaskDelay(pdMS_TO_TICKS(650));

        while (readReg(REG_CHIP_ID) != BNO055_ID) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        vTaskDelay(pdMS_TO_TICKS(50));

        writeReg(REG_PWR_MODE, PWR_NORMAL);
        vTaskDelay(pdMS_TO_TICKS(10));

        writeReg(REG_PAGE_ID, 0);
        writeReg(REG_SYS_TRIGGER, 0x80);
        vTaskDelay(pdMS_TO_TICKS(10));

        writeReg(REG_OPR_MODE, MODE_NDOF);
        vTaskDelay(pdMS_TO_TICKS(20));

        _initialized = true;
        return true;
    }

    bool update() {
        if (!_initialized) return false;

        uint8_t buf[6];
        if (I2CBus::instance().readReg(_addr, REG_EULER_H_LSB, buf, 6) != ESP_OK) return false;

        int16_t h = (buf[1] << 8) | buf[0];
        int16_t r = (buf[3] << 8) | buf[2];
        int16_t p = (buf[5] << 8) | buf[4];

        _euler[0] = h / 16.0f;
        _euler[1] = r / 16.0f;
        _euler[2] = p / 16.0f;

        return true;
    }

    bool calibrate() {
        if (!_initialized) return false;

        uint8_t calData[22];

        writeReg(REG_OPR_MODE, MODE_CONFIG);
        vTaskDelay(pdMS_TO_TICKS(25));

        if (I2CBus::instance().readReg(_addr, REG_ACCEL_OFFSET_X_LSB, calData, 22) != ESP_OK) {
            writeReg(REG_OPR_MODE, MODE_NDOF);
            return false;
        }

        writeReg(REG_OPR_MODE, MODE_NDOF);
        vTaskDelay(pdMS_TO_TICKS(20));

        return true;
    }

    float getHeading() const { return _euler[0]; }
    float getRoll() const { return _euler[1]; }
    float getPitch() const { return _euler[2]; }
    bool isInitialized() const { return _initialized; }

    uint8_t getCalibrationStatus() { return readReg(REG_CALIB_STAT); }

  private:
    static constexpr uint8_t BNO055_ID = 0xA0;
    static constexpr uint8_t REG_CHIP_ID = 0x00;
    static constexpr uint8_t REG_PAGE_ID = 0x07;
    static constexpr uint8_t REG_ACCEL_OFFSET_X_LSB = 0x55;
    static constexpr uint8_t REG_OPR_MODE = 0x3D;
    static constexpr uint8_t REG_PWR_MODE = 0x3E;
    static constexpr uint8_t REG_SYS_TRIGGER = 0x3F;
    static constexpr uint8_t REG_EULER_H_LSB = 0x1A;
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

    uint8_t _addr;
    bool _initialized = false;
    float _euler[3] = {0};
};
