#pragma once

#include <peripherals/i2c_bus.h>
#include <cmath>

class HMC5883LDriver {
  public:
    static constexpr uint8_t DEFAULT_ADDR = 0x1E;

    HMC5883LDriver(uint8_t addr = DEFAULT_ADDR) : _addr(addr) {}

    bool begin() {
        if (!I2CBus::instance().probe(_addr)) return false;

        uint8_t idA = readReg(REG_ID_A);
        uint8_t idB = readReg(REG_ID_B);
        uint8_t idC = readReg(REG_ID_C);
        if (idA != 'H' || idB != '4' || idC != '3') return false;

        writeReg(REG_CONFIG_A, 0x70);
        writeReg(REG_CONFIG_B, 0x20);
        writeReg(REG_MODE, 0x00);

        _initialized = true;
        return true;
    }

    bool update() {
        if (!_initialized) return false;

        uint8_t buf[6];
        if (I2CBus::instance().readReg(_addr, REG_DATA_X_MSB, buf, 6) != ESP_OK) return false;

        int16_t x = (buf[0] << 8) | buf[1];
        int16_t z = (buf[2] << 8) | buf[3];
        int16_t y = (buf[4] << 8) | buf[5];

        _mag[0] = x * _scale;
        _mag[1] = y * _scale;
        _mag[2] = z * _scale;

        _heading = atan2f(_mag[1], _mag[0]);
        _heading += _declination;
        if (_heading < 0) _heading += 2 * M_PI;
        if (_heading > 2 * M_PI) _heading -= 2 * M_PI;
        _heading *= 180.0f / M_PI;

        return true;
    }

    void setDeclination(float dec) { _declination = dec; }
    float getMagX() const { return _mag[0]; }
    float getMagY() const { return _mag[1]; }
    float getMagZ() const { return _mag[2]; }
    float getHeading() const { return _heading; }
    bool isInitialized() const { return _initialized; }

  private:
    static constexpr uint8_t REG_CONFIG_A = 0x00;
    static constexpr uint8_t REG_CONFIG_B = 0x01;
    static constexpr uint8_t REG_MODE = 0x02;
    static constexpr uint8_t REG_DATA_X_MSB = 0x03;
    static constexpr uint8_t REG_ID_A = 0x0A;
    static constexpr uint8_t REG_ID_B = 0x0B;
    static constexpr uint8_t REG_ID_C = 0x0C;
    static constexpr float _scale = 0.92f;

    void writeReg(uint8_t reg, uint8_t val) { I2CBus::instance().writeReg(_addr, reg, &val, 1); }

    uint8_t readReg(uint8_t reg) {
        uint8_t val = 0;
        I2CBus::instance().readReg(_addr, reg, &val, 1);
        return val;
    }

    uint8_t _addr;
    bool _initialized = false;
    float _mag[3] = {0};
    float _heading = 0;
    float _declination = 0.22f;
};
