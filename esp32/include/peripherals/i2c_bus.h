#pragma once

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include <vector>
#include <cstring>

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

        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.i2c_port = port;
        bus_cfg.sda_io_num = sda;
        bus_cfg.scl_io_num = scl;
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.glitch_ignore_cnt = 7;
#if CONFIG_IDF_TARGET_ESP32P4
        bus_cfg.flags.enable_internal_pullup = false;
#else
        bus_cfg.flags.enable_internal_pullup = true;
#endif

        esp_err_t err = i2c_new_master_bus(&bus_cfg, &_bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
            return err;
        }

        _initialized = true;
        return ESP_OK;
    }

    void end() {
        if (_initialized) {
            if (_dev) {
                i2c_master_bus_rm_device(_dev);
                _dev = NULL;
                _dev_addr = 0xFF;
            }
            i2c_del_master_bus(_bus);
            _bus = NULL;
            _initialized = false;
        }
    }

    bool isInitialized() const { return _initialized; }

    i2c_master_bus_handle_t busHandle() const { return _bus; }

    esp_err_t writeBytes(uint8_t addr, const uint8_t* data, size_t len) {
        if (!_initialized) return ESP_ERR_INVALID_STATE;
        esp_err_t err = ensureDevice(addr);
        if (err != ESP_OK) return err;
        return i2c_master_transmit(_dev, data, len, pdMS_TO_TICKS(200));
    }

    esp_err_t writeReg(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) {
        if (!_initialized) return ESP_ERR_INVALID_STATE;
        esp_err_t err = ensureDevice(addr);
        if (err != ESP_OK) return err;

        uint8_t buf[len + 1];
        buf[0] = reg;
        if (len > 0 && data != nullptr) {
            memcpy(buf + 1, data, len);
        }
        return i2c_master_transmit(_dev, buf, len + 1, pdMS_TO_TICKS(200));
    }

    esp_err_t readReg(uint8_t addr, uint8_t reg, uint8_t* data, size_t len) {
        if (!_initialized) return ESP_ERR_INVALID_STATE;
        esp_err_t err = ensureDevice(addr);
        if (err != ESP_OK) return err;
        return i2c_master_transmit_receive(_dev, &reg, 1, data, len, pdMS_TO_TICKS(200));
    }

    bool probe(uint8_t addr) {
        if (!_initialized) return false;
        return i2c_master_probe(_bus, addr, pdMS_TO_TICKS(200)) == ESP_OK;
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

    i2c_master_bus_handle_t _bus = NULL;
    i2c_master_dev_handle_t _dev = NULL;
    uint8_t _dev_addr = 0xFF;

    esp_err_t ensureDevice(uint8_t addr) {
        if (_dev && _dev_addr == addr) return ESP_OK;
        if (_dev) {
            i2c_master_bus_rm_device(_dev);
            _dev = NULL;
        }
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = addr;
        dev_cfg.scl_speed_hz = _freq;
        esp_err_t err = i2c_master_bus_add_device(_bus, &dev_cfg, &_dev);
        if (err == ESP_OK) _dev_addr = addr;
        return err;
    }
};
