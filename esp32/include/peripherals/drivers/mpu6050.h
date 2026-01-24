#pragma once

#include <peripherals/i2c_bus.h>
#include <utils/math_utils.h>

class MPU6050Driver {
  public:
    static constexpr uint8_t DEFAULT_ADDR = 0x68;

    MPU6050Driver(uint8_t addr = DEFAULT_ADDR) : _addr(addr) {}

    bool begin() {
        if (!I2CBus::instance().probe(_addr)) return false;

        writeReg(REG_PWR_MGMT_1, 0x80);
        vTaskDelay(pdMS_TO_TICKS(100));

        writeReg(REG_PWR_MGMT_1, 0x01);
        writeReg(REG_PWR_MGMT_2, 0x00);
        vTaskDelay(pdMS_TO_TICKS(50));

        uint8_t who = readReg(REG_WHO_AM_I);
        if (who != 0x68 && who != 0x98 && who != 0x72) return false;

        setI2CMasterModeEnabled(false);
        setI2CBypassEnabled(true);
        setSleepEnabled(false);

        writeReg(REG_SMPLRT_DIV, 4);
        writeReg(REG_CONFIG, 0x03);
        writeReg(REG_GYRO_CONFIG, 0x00);
        writeReg(REG_ACCEL_CONFIG, 0x00);
        writeReg(REG_INT_PIN_CFG, 0x02);
        writeReg(REG_INT_ENABLE, 0x01);

        resetFIFO();

        setGyroOffsets(0, 0, 0);
        setAccelOffsets(0, 0, 0);

        _initialized = true;
        return true;
    }

    bool update() {
        if (!_initialized) return false;

        uint8_t buf[14];
        if (I2CBus::instance().readReg(_addr, REG_ACCEL_XOUT_H, buf, 14) != ESP_OK) return false;

        int16_t rawAx = (buf[0] << 8) | buf[1];
        int16_t rawAy = (buf[2] << 8) | buf[3];
        int16_t rawAz = (buf[4] << 8) | buf[5];
        int16_t rawTemp = (buf[6] << 8) | buf[7];
        int16_t rawGx = (buf[8] << 8) | buf[9];
        int16_t rawGy = (buf[10] << 8) | buf[11];
        int16_t rawGz = (buf[12] << 8) | buf[13];

        _accel[0] = (rawAx - _accelOffset[0]) / 16384.0f;
        _accel[1] = (rawAy - _accelOffset[1]) / 16384.0f;
        _accel[2] = (rawAz - _accelOffset[2]) / 16384.0f;
        _gyro[0] = (rawGx - _gyroOffset[0]) / 131.0f;
        _gyro[1] = (rawGy - _gyroOffset[1]) / 131.0f;
        _gyro[2] = (rawGz - _gyroOffset[2]) / 131.0f;
        _temp = rawTemp / 340.0f + 36.53f;

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        float dt = (now - _lastUpdate) / 1000.0f;
        if (dt <= 0 || dt > 1.0f) dt = 0.02f;
        _lastUpdate = now;

        float accelMag = sqrtf(_accel[0] * _accel[0] + _accel[1] * _accel[1] + _accel[2] * _accel[2]);
        bool accelValid = (accelMag > 0.8f && accelMag < 1.2f);

        float accelRoll = atan2f(_accel[1], _accel[2]) * RAD2DEG_F;
        float accelPitch = atan2f(-_accel[0], sqrtf(_accel[1] * _accel[1] + _accel[2] * _accel[2])) * RAD2DEG_F;

        if (accelValid) {
            _roll = ALPHA * (_roll + _gyro[0] * dt) + (1.0f - ALPHA) * accelRoll;
            _pitch = ALPHA * (_pitch + _gyro[1] * dt) + (1.0f - ALPHA) * accelPitch;
        } else {
            _roll += _gyro[0] * dt;
            _pitch += _gyro[1] * dt;
        }
        _yaw += _gyro[2] * dt;

        return true;
    }

    bool calibrate() {
        if (!_initialized) return false;

        _gyroOffset[0] = 0;
        _gyroOffset[1] = 0;
        _gyroOffset[2] = 0;
        _accelOffset[0] = 0;
        _accelOffset[1] = 0;
        _accelOffset[2] = 0;

        int32_t gxSum = 0, gySum = 0, gzSum = 0;
        int32_t axSum = 0, aySum = 0, azSum = 0;
        int validSamples = 0;

        for (int iteration = 0; iteration < 6; iteration++) {
            gxSum = gySum = gzSum = 0;
            axSum = aySum = azSum = 0;
            validSamples = 0;

            for (int i = 0; i < 100; i++) {
                uint8_t buf[14];
                if (I2CBus::instance().readReg(_addr, REG_ACCEL_XOUT_H, buf, 14) != ESP_OK) continue;

                axSum += (int16_t)((buf[0] << 8) | buf[1]);
                aySum += (int16_t)((buf[2] << 8) | buf[3]);
                azSum += (int16_t)((buf[4] << 8) | buf[5]);
                gxSum += (int16_t)((buf[8] << 8) | buf[9]);
                gySum += (int16_t)((buf[10] << 8) | buf[11]);
                gzSum += (int16_t)((buf[12] << 8) | buf[13]);
                validSamples++;
                vTaskDelay(pdMS_TO_TICKS(2));
            }

            if (validSamples == 0) return false;

            _gyroOffset[0] += gxSum / validSamples;
            _gyroOffset[1] += gySum / validSamples;
            _gyroOffset[2] += gzSum / validSamples;
            _accelOffset[0] += axSum / validSamples;
            _accelOffset[1] += aySum / validSamples;
            _accelOffset[2] += (azSum / validSamples) - 16384;
        }

        _roll = 0;
        _pitch = 0;
        _yaw = 0;
        _lastUpdate = xTaskGetTickCount() * portTICK_PERIOD_MS;

        return true;
    }

    void setGyroOffsets(int16_t x, int16_t y, int16_t z) {
        uint8_t buf[6] = {static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x & 0xFF),
                          static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y & 0xFF),
                          static_cast<uint8_t>(z >> 8), static_cast<uint8_t>(z & 0xFF)};
        I2CBus::instance().writeReg(_addr, REG_XG_OFFSET_H, buf, 6);
    }

    void setAccelOffsets(int16_t x, int16_t y, int16_t z) {
        uint8_t buf[2];
        buf[0] = x >> 8;
        buf[1] = x & 0xFF;
        I2CBus::instance().writeReg(_addr, REG_XA_OFFSET_H, buf, 2);
        buf[0] = y >> 8;
        buf[1] = y & 0xFF;
        I2CBus::instance().writeReg(_addr, REG_YA_OFFSET_H, buf, 2);
        buf[0] = z >> 8;
        buf[1] = z & 0xFF;
        I2CBus::instance().writeReg(_addr, REG_ZA_OFFSET_H, buf, 2);
    }

    void setI2CMasterModeEnabled(bool enabled) {
        uint8_t val = readReg(REG_USER_CTRL);
        writeReg(REG_USER_CTRL, enabled ? (val | 0x20) : (val & ~0x20));
    }

    void setI2CBypassEnabled(bool enabled) {
        uint8_t val = readReg(REG_INT_PIN_CFG);
        writeReg(REG_INT_PIN_CFG, enabled ? (val | 0x02) : (val & ~0x02));
    }

    void setSleepEnabled(bool enabled) {
        uint8_t val = readReg(REG_PWR_MGMT_1);
        writeReg(REG_PWR_MGMT_1, enabled ? (val | 0x40) : (val & ~0x40));
    }

    void resetFIFO() { writeReg(REG_USER_CTRL, readReg(REG_USER_CTRL) | 0x04); }

    void setFIFOEnabled(bool enabled) {
        uint8_t val = readReg(REG_USER_CTRL);
        writeReg(REG_USER_CTRL, enabled ? (val | 0x40) : (val & ~0x40));
    }

    float getRoll() const { return _roll; }
    float getPitch() const { return _pitch; }
    float getYaw() const { return _yaw; }
    float getTemperature() const { return _temp; }
    const float* getAccel() const { return _accel; }
    const float* getGyro() const { return _gyro; }
    bool isInitialized() const { return _initialized; }

  private:
    static constexpr uint8_t REG_XG_OFFSET_H = 0x13;
    static constexpr uint8_t REG_XA_OFFSET_H = 0x06;
    static constexpr uint8_t REG_YA_OFFSET_H = 0x08;
    static constexpr uint8_t REG_ZA_OFFSET_H = 0x0A;
    static constexpr uint8_t REG_SMPLRT_DIV = 0x19;
    static constexpr uint8_t REG_CONFIG = 0x1A;
    static constexpr uint8_t REG_GYRO_CONFIG = 0x1B;
    static constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
    static constexpr uint8_t REG_FIFO_EN = 0x23;
    static constexpr uint8_t REG_INT_PIN_CFG = 0x37;
    static constexpr uint8_t REG_INT_ENABLE = 0x38;
    static constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
    static constexpr uint8_t REG_USER_CTRL = 0x6A;
    static constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
    static constexpr uint8_t REG_PWR_MGMT_2 = 0x6C;
    static constexpr uint8_t REG_FIFO_COUNT_H = 0x72;
    static constexpr uint8_t REG_FIFO_R_W = 0x74;
    static constexpr uint8_t REG_WHO_AM_I = 0x75;

    static constexpr float ALPHA = 0.98f;

    void writeReg(uint8_t reg, uint8_t val) { I2CBus::instance().writeReg(_addr, reg, &val, 1); }

    uint8_t readReg(uint8_t reg) {
        uint8_t val = 0;
        I2CBus::instance().readReg(_addr, reg, &val, 1);
        return val;
    }

    uint8_t _addr;
    bool _initialized = false;
    float _accel[3] = {0};
    float _gyro[3] = {0};
    int16_t _gyroOffset[3] = {0};
    int16_t _accelOffset[3] = {0};
    float _temp = 0;
    float _roll = 0;
    float _pitch = 0;
    float _yaw = 0;
    uint32_t _lastUpdate = 0;
};
