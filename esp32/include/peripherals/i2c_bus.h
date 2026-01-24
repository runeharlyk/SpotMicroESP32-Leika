#pragma once

#include <driver/i2c.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include <vector>

class I2CBus {
  public:
    static I2CBus& instance() {
        static I2CBus inst;
        return inst;
    }

    esp_err_t begin(gpio_num_t sda, gpio_num_t scl, uint32_t freq = 100000, i2c_port_t port = I2C_NUM_0) {
        if (_initialized) {
            end();
        }

        _port = port;
        _sda = sda;
        _scl = scl;
        _freq = freq;

        i2c_config_t conf = {};
        conf.mode = I2C_MODE_MASTER;
        conf.sda_io_num = sda;
        conf.scl_io_num = scl;
        conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
        conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
        conf.master.clk_speed = freq;

        esp_err_t err = i2c_param_config(_port, &conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
            return err;
        }

        err = i2c_driver_install(_port, I2C_MODE_MASTER, 0, 0, 0);
        if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "I2C driver already installed for port %d, reconfiguring", _port);
            i2c_driver_delete(_port);
            err = i2c_param_config(_port, &conf);
            if (err != ESP_OK) return err;
            err = i2c_driver_install(_port, I2C_MODE_MASTER, 0, 0, 0);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
            return err;
        }

        _initialized = true;
        return ESP_OK;
    }

    void end() {
        if (_initialized) {
            i2c_driver_delete(_port);
            _initialized = false;
        }
    }

    bool isInitialized() const { return _initialized; }

    esp_err_t writeBytes(uint8_t addr, const uint8_t* data, size_t len) {
        if (!_initialized) return ESP_ERR_INVALID_STATE;

        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        if (len > 0 && data != nullptr) {
            i2c_master_write(cmd, data, len, true);
        }
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(_port, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        return ret;
    }

    esp_err_t writeReg(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) {
        if (!_initialized) return ESP_ERR_INVALID_STATE;

        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg, true);
        if (len > 0 && data != nullptr) {
            i2c_master_write(cmd, data, len, true);
        }
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(_port, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        return ret;
    }

    esp_err_t readReg(uint8_t addr, uint8_t reg, uint8_t* data, size_t len) {
        if (!_initialized) return ESP_ERR_INVALID_STATE;

        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg, true);
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
        if (len > 1) {
            i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
        }
        i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(_port, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        return ret;
    }

    bool probe(uint8_t addr) {
        if (!_initialized) return false;

        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(_port, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        return ret == ESP_OK;
    }

    std::vector<uint8_t> scan(uint8_t lower = 1, uint8_t upper = 127) {
        std::vector<uint8_t> devices;
        if (!_initialized) return devices;

        for (uint8_t addr = lower; addr < upper; addr++) {
            if (probe(addr)) {
                devices.push_back(addr);
                ESP_LOGI(TAG, "I2C device found at address 0x%02X", addr);
            }
        }
        ESP_LOGI(TAG, "Scan complete - Found %d device(s)", devices.size());
        return devices;
    }

    i2c_port_t port() const { return _port; }
    gpio_num_t sda() const { return _sda; }
    gpio_num_t scl() const { return _scl; }
    uint32_t freq() const { return _freq; }

  private:
    I2CBus() = default;
    ~I2CBus() { end(); }
    I2CBus(const I2CBus&) = delete;
    I2CBus& operator=(const I2CBus&) = delete;

    static constexpr const char* TAG = "I2CBus";
    i2c_port_t _port = I2C_NUM_0;
    gpio_num_t _sda = GPIO_NUM_NC;
    gpio_num_t _scl = GPIO_NUM_NC;
    uint32_t _freq = 100000;
    bool _initialized = false;
};
