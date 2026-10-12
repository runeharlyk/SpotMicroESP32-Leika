#pragma once

#include <peripherals/i2c_bus.h>
#include <cmath>
#include <cstdint>

/**
 * The INA219's calibration for a shunt and the largest current expected through it (datasheet SBOS448G, 8.5.1): the
 * current's LSB is that current over 2^15, raised where the calibration register would overflow, and the smallest
 * shunt range (PGA gain) that holds that current across the shunt.
 */
struct Ina219Calibration {
    uint16_t calibration;
    float currentLsbA;
    uint8_t pga; // PG1:PG0, 0 for +-40 mV up to 3 for +-320 mV

    static Ina219Calibration forShunt(float shuntOhms, float maxCurrentA) {
        float currentLsb = maxCurrentA / 32768.0f;
        float calibration = std::trunc(0.04096f / (currentLsb * shuntOhms));
        // FS0 is void, so the register holds at most 0xFFFE.
        if (calibration > 0xFFFE) {
            calibration = 0xFFFE;
            currentLsb = 0.04096f / (calibration * shuntOhms);
        }
        const float shuntVolts = maxCurrentA * shuntOhms;
        uint8_t pga = 0;
        while (pga < 3 && shuntVolts > 0.04f * (1 << pga)) pga++;
        return {static_cast<uint16_t>(static_cast<uint16_t>(calibration) & 0xFFFE), currentLsb, pga};
    }
};

/** One reading of the rail: the bus voltage on the load side of the shunt, and the current through it. */
struct PowerReading {
    float busVoltage = 0;   // V
    float shuntVoltage = 0; // V
    float current = 0;      // A
    float power = 0;        // W
};

/** Register values to SI units: the bus value sits in bits 15-3 at 4 mV, the shunt at 10 uV, the current at its LSB. */
inline PowerReading ina219Reading(uint16_t busRegister, int16_t shuntRegister, int16_t currentRegister,
                                  float currentLsbA) {
    PowerReading reading;
    reading.busVoltage = (busRegister >> 3) * 0.004f;
    reading.shuntVoltage = shuntRegister * 10e-6f;
    reading.current = currentRegister * currentLsbA;
    reading.power = reading.busVoltage * reading.current;
    return reading;
}

class INA219Driver {
  public:
    static constexpr uint8_t DEFAULT_ADDR = 0x41;

    void setAddress(uint8_t addr) {
        _addr = addr;
        _initialized = false;
    }

    /**
     * Whether an INA219 answers, without configuring it. The chip has no ID register, so this checks the bits the
     * datasheet fixes at 0: the configuration's bit 14, the bus voltage's bit 2 and the calibration's FS0.
     */
    bool identify() {
        if (!I2CBus::instance().probe(_addr)) return false;
        uint16_t config, bus, calibration;
        return readReg(REG_CONFIG, config) && readReg(REG_BUS, bus) && readReg(REG_CALIBRATION, calibration) &&
               !(config & 0x4000) && !(bus & 0x0004) && !(calibration & 0x0001);
    }

    bool begin(float shuntOhms, float maxCurrentA) {
        _initialized = false;
        if (!identify()) return false;
        _calibration = Ina219Calibration::forShunt(shuntOhms, maxCurrentA);
        _initialized = configure();
        return _initialized;
    }

    void stop() { _initialized = false; }

    /**
     * Reads the rail. A chip that reset since, by a dip on its own supply, reads back its default configuration and
     * an empty calibration, which would report no current: it is configured again first.
     */
    bool read(PowerReading &reading) {
        if (!_initialized) return false;
        uint16_t config, bus, shunt, current;
        if (!readReg(REG_CONFIG, config)) return false;
        if (config != configValue() && !configure()) return false;
        if (!readReg(REG_BUS, bus) || !readReg(REG_SHUNT, shunt) || !readReg(REG_CURRENT, current)) return false;
        reading =
            ina219Reading(bus, static_cast<int16_t>(shunt), static_cast<int16_t>(current), _calibration.currentLsbA);
        return true;
    }

    bool active() const { return _initialized; }
    const Ina219Calibration &calibration() const { return _calibration; }

    /**
     * The 32 V bus range (the chip takes up to 26 V), the calibration's shunt range, the bus averaged over 16 samples
     * (8.5 ms, so a dip shows) and the shunt over 128 (68 ms, most of the 100 ms between reads), both continuous.
     */
    uint16_t configValue() const {
        return BRNG_32V | (_calibration.pga << 11) | (ADC_16_SAMPLES << 7) | (ADC_128_SAMPLES << 3) | MODE_CONTINUOUS;
    }

  private:
    static constexpr uint8_t REG_CONFIG = 0x00;
    static constexpr uint8_t REG_SHUNT = 0x01;
    static constexpr uint8_t REG_BUS = 0x02;
    static constexpr uint8_t REG_CURRENT = 0x04;
    static constexpr uint8_t REG_CALIBRATION = 0x05;
    static constexpr uint16_t BRNG_32V = 1 << 13;
    static constexpr uint16_t ADC_16_SAMPLES = 0xC;
    static constexpr uint16_t ADC_128_SAMPLES = 0xF;
    static constexpr uint16_t MODE_CONTINUOUS = 0x7;

    bool configure() {
        uint16_t config;
        return writeReg(REG_CONFIG, configValue()) && writeReg(REG_CALIBRATION, _calibration.calibration) &&
               readReg(REG_CONFIG, config) && config == configValue();
    }

    bool writeReg(uint8_t reg, uint16_t value) {
        const uint8_t bytes[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
        return I2CBus::instance().writeReg(_addr, reg, bytes, 2) == ESP_OK;
    }

    bool readReg(uint8_t reg, uint16_t &value) {
        uint8_t bytes[2];
        if (I2CBus::instance().readReg(_addr, reg, bytes, 2) != ESP_OK) return false;
        value = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
        return true;
    }

    uint8_t _addr = DEFAULT_ADDR;
    bool _initialized = false;
    Ina219Calibration _calibration {};
};
