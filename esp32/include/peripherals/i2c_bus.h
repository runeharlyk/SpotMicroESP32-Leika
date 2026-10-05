#pragma once

#include <algorithm>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include <map>
#include <vector>

/**
 * The robot's I2C bus, shared by the control task (servos), the sensors and the socket's task (scans,
 * pin changes). Every operation holds one recursive lock, so a restart cannot free a device another
 * task is using, and each address keeps its device handle for the bus's lifetime instead of one
 * handle being removed and re-added on every switch between the servo board and the IMU.
 */
class I2CBus {
  public:
    static I2CBus& instance() {
        static I2CBus inst;
        return inst;
    }

    // The bus is clocked for its slowest device: the IMUs, compass and gesture sensor are rated for 400 kHz.
    static constexpr uint32_t MAX_FREQUENCY = 400000;

    esp_err_t begin(gpio_num_t sda, gpio_num_t scl, uint32_t freq = 100000, i2c_port_t port = I2C_NUM_0) {
        Lock lock(_mutex);
        if (_initialized) {
            end();
        }

        _port = port;
        _sda = sda;
        _scl = scl;
        _freq = std::min(freq, MAX_FREQUENCY);

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
        Lock lock(_mutex);
        if (_initialized) {
            for (auto &[address, device] : _devices) i2c_master_bus_rm_device(device);
            _devices.clear();
            i2c_del_master_bus(_bus);
            _bus = NULL;
            _initialized = false;
        }
    }

    bool isInitialized() const { return _initialized; }

    i2c_master_bus_handle_t busHandle() const { return _bus; }

    esp_err_t writeBytes(uint8_t addr, const uint8_t* data, size_t len) {
        Lock lock(_mutex);
        i2c_master_dev_handle_t device;
        esp_err_t err = deviceAt(addr, device);
        if (err != ESP_OK) return err;
        return i2c_master_transmit(device, data, len, TIMEOUT_MS);
    }

    esp_err_t writeReg(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) {
        Lock lock(_mutex);
        i2c_master_dev_handle_t device;
        esp_err_t err = deviceAt(addr, device);
        if (err != ESP_OK) return err;

        i2c_master_transmit_multi_buffer_info_t parts[] = {{&reg, 1}, {data, data != nullptr ? len : 0}};
        return i2c_master_multi_buffer_transmit(device, parts, 2, TIMEOUT_MS);
    }

    esp_err_t readReg(uint8_t addr, uint8_t reg, uint8_t* data, size_t len) {
        Lock lock(_mutex);
        i2c_master_dev_handle_t device;
        esp_err_t err = deviceAt(addr, device);
        if (err != ESP_OK) return err;
        return i2c_master_transmit_receive(device, &reg, 1, data, len, TIMEOUT_MS);
    }

    bool probe(uint8_t addr) {
        Lock lock(_mutex);
        if (!_initialized) return false;
        return i2c_master_probe(_bus, addr, TIMEOUT_MS) == ESP_OK;
    }

    // Locks per probe rather than for the whole scan, so the servos keep being written while it runs.
    std::vector<uint8_t> scan(uint8_t lower = 1, uint8_t upper = 127) {
        std::vector<uint8_t> devices;
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
    struct Lock {
        explicit Lock(SemaphoreHandle_t mutex) : _mutex(mutex) { xSemaphoreTakeRecursive(_mutex, portMAX_DELAY); }
        ~Lock() { xSemaphoreGiveRecursive(_mutex); }
        SemaphoreHandle_t _mutex;
    };

    I2CBus() : _mutex(xSemaphoreCreateRecursiveMutex()) {}
    ~I2CBus() { end(); }
    I2CBus(const I2CBus&) = delete;
    I2CBus& operator=(const I2CBus&) = delete;

    static constexpr const char* TAG = "I2CBus";
    // The driver takes milliseconds, not ticks. Long enough for any transfer here; short, because a missing
    // device costs the control loop this long on every write.
    static constexpr int TIMEOUT_MS = 20;
    i2c_port_t _port = I2C_NUM_0;
    gpio_num_t _sda = GPIO_NUM_NC;
    gpio_num_t _scl = GPIO_NUM_NC;
    uint32_t _freq = 100000;
    bool _initialized = false;

    SemaphoreHandle_t _mutex;
    i2c_master_bus_handle_t _bus = NULL;
    std::map<uint8_t, i2c_master_dev_handle_t> _devices;

    // Called with the lock held.
    esp_err_t deviceAt(uint8_t addr, i2c_master_dev_handle_t &device) {
        if (!_initialized) return ESP_ERR_INVALID_STATE;
        auto known = _devices.find(addr);
        if (known != _devices.end()) {
            device = known->second;
            return ESP_OK;
        }
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = addr;
        dev_cfg.scl_speed_hz = _freq;
        esp_err_t err = i2c_master_bus_add_device(_bus, &dev_cfg, &device);
        if (err == ESP_OK) _devices[addr] = device;
        return err;
    }
};
