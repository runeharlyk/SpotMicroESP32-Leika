#pragma once

#include <peripherals/i2c_bus.h>
#include <cmath>

class BMP180Driver {
  public:
    static constexpr uint8_t DEFAULT_ADDR = 0x77;
    static constexpr float SEA_LEVEL_HPA = 1013.25f;

    BMP180Driver(uint8_t addr = DEFAULT_ADDR) : _addr(addr) {}

    bool begin() {
        if (!I2CBus::instance().probe(_addr)) return false;

        uint8_t id = readReg8(REG_CHIP_ID);
        if (id != 0x55) return false;

        _ac1 = readReg16(0xAA);
        _ac2 = readReg16(0xAC);
        _ac3 = readReg16(0xAE);
        _ac4 = readReg16U(0xB0);
        _ac5 = readReg16U(0xB2);
        _ac6 = readReg16U(0xB4);
        _b1 = readReg16(0xB6);
        _b2 = readReg16(0xB8);
        _mb = readReg16(0xBA);
        _mc = readReg16(0xBC);
        _md = readReg16(0xBE);

        _initialized = true;
        return true;
    }

    bool update() {
        if (!_initialized) return false;

        writeReg(REG_CONTROL, CMD_TEMP);
        vTaskDelay(pdMS_TO_TICKS(5));
        int32_t ut = readReg16(REG_OUT_MSB);

        writeReg(REG_CONTROL, CMD_PRESSURE + (_oss << 6));
        vTaskDelay(pdMS_TO_TICKS(2 + (3 << _oss)));

        int32_t up = readReg24(REG_OUT_MSB) >> (8 - _oss);

        int32_t x1 = ((ut - _ac6) * _ac5) >> 15;
        int32_t x2 = (_mc << 11) / (x1 + _md);
        int32_t b5 = x1 + x2;
        _temperature = ((b5 + 8) >> 4) / 10.0f;

        int32_t b6 = b5 - 4000;
        x1 = (_b2 * ((b6 * b6) >> 12)) >> 11;
        x2 = (_ac2 * b6) >> 11;
        int32_t x3 = x1 + x2;
        int32_t b3 = (((_ac1 * 4 + x3) << _oss) + 2) >> 2;
        x1 = (_ac3 * b6) >> 13;
        x2 = (_b1 * ((b6 * b6) >> 12)) >> 16;
        x3 = ((x1 + x2) + 2) >> 2;
        uint32_t b4 = (_ac4 * (uint32_t)(x3 + 32768)) >> 15;
        uint32_t b7 = ((uint32_t)up - b3) * (50000 >> _oss);
        int32_t p;
        if (b7 < 0x80000000) {
            p = (b7 << 1) / b4;
        } else {
            p = (b7 / b4) << 1;
        }
        x1 = (p >> 8) * (p >> 8);
        x1 = (x1 * 3038) >> 16;
        x2 = (-7357 * p) >> 16;
        p = p + ((x1 + x2 + 3791) >> 4);

        _pressure = p / 100.0f;
        _altitude = 44330.0f * (1.0f - powf(_pressure / SEA_LEVEL_HPA, 0.1903f));

        return true;
    }

    float getPressure() const { return _pressure; }
    float getAltitude() const { return _altitude; }
    float getTemperature() const { return _temperature; }
    bool isInitialized() const { return _initialized; }

  private:
    static constexpr uint8_t REG_CHIP_ID = 0xD0;
    static constexpr uint8_t REG_CONTROL = 0xF4;
    static constexpr uint8_t REG_OUT_MSB = 0xF6;
    static constexpr uint8_t CMD_TEMP = 0x2E;
    static constexpr uint8_t CMD_PRESSURE = 0x34;

    void writeReg(uint8_t reg, uint8_t val) { I2CBus::instance().writeReg(_addr, reg, &val, 1); }

    uint8_t readReg8(uint8_t reg) {
        uint8_t val = 0;
        I2CBus::instance().readReg(_addr, reg, &val, 1);
        return val;
    }

    int16_t readReg16(uint8_t reg) {
        uint8_t buf[2];
        I2CBus::instance().readReg(_addr, reg, buf, 2);
        return (int16_t)((buf[0] << 8) | buf[1]);
    }

    uint16_t readReg16U(uint8_t reg) {
        uint8_t buf[2];
        I2CBus::instance().readReg(_addr, reg, buf, 2);
        return (uint16_t)((buf[0] << 8) | buf[1]);
    }

    int32_t readReg24(uint8_t reg) {
        uint8_t buf[3];
        I2CBus::instance().readReg(_addr, reg, buf, 3);
        return ((int32_t)buf[0] << 16) | ((int32_t)buf[1] << 8) | buf[2];
    }

    uint8_t _addr;
    bool _initialized = false;
    uint8_t _oss = 0;

    int16_t _ac1, _ac2, _ac3;
    uint16_t _ac4, _ac5, _ac6;
    int16_t _b1, _b2;
    int16_t _mb, _mc, _md;

    float _temperature = 0;
    float _pressure = 0;
    float _altitude = 0;
};
