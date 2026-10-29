#pragma once

#include <algorithm>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
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
        return transfer(addr, [&](i2c_master_dev_handle_t device) {
            return i2c_master_transmit(device, data, len, TIMEOUT_MS);
        });
    }

    /** Where a transfer's time went, in esp_timer microseconds: waiting while another task held the bus, then on it. */
    struct Timing {
        int64_t waitUs = 0;
        int64_t transferUs = 0;
    };

    esp_err_t writeReg(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len, Timing* timing = nullptr) {
        return transfer(
            addr,
            [&](i2c_master_dev_handle_t device) {
                i2c_master_transmit_multi_buffer_info_t parts[] = {{&reg, 1}, {data, data != nullptr ? len : 0}};
                return i2c_master_multi_buffer_transmit(device, parts, 2, TIMEOUT_MS);
            },
            timing);
    }

    esp_err_t readReg(uint8_t addr, uint8_t reg, uint8_t* data, size_t len) {
        return transfer(addr, [&](i2c_master_dev_handle_t device) {
            return i2c_master_transmit_receive(device, &reg, 1, data, len, TIMEOUT_MS);
        });
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

    struct DeviceErrors {
        uint8_t address;
        uint32_t nacks;
        uint32_t timeouts;
    };

    // Every device the bus has transferred with since boot, in address order, with its failed transfers.
    std::vector<DeviceErrors> errors() {
        Lock lock(_mutex);
        std::vector<DeviceErrors> devices;
        for (const auto &[address, counts] : _errors) devices.push_back({address, counts.nacks, counts.timeouts});
        return devices;
    }

    // Times since boot the bus freed a device that held SDA low.
    uint32_t recoveries() {
        Lock lock(_mutex);
        return _recoveries;
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
    // A recovery restarts the bus, which costs more than a transfer: a device that holds SDA for good is not
    // recovered on every one.
    static constexpr int64_t RECOVERY_INTERVAL_US = 1000000;
    i2c_port_t _port = I2C_NUM_0;
    gpio_num_t _sda = GPIO_NUM_NC;
    gpio_num_t _scl = GPIO_NUM_NC;
    uint32_t _freq = 100000;
    bool _initialized = false;

    struct ErrorCounts {
        uint32_t nacks = 0;
        uint32_t timeouts = 0;
    };

    SemaphoreHandle_t _mutex;
    i2c_master_bus_handle_t _bus = NULL;
    std::map<uint8_t, i2c_master_dev_handle_t> _devices;
    // Kept across restarts of the bus, as the analytics count since boot.
    std::map<uint8_t, ErrorCounts> _errors;
    int64_t _nextRecoveryUs = 0;
    uint32_t _recoveries = 0;

    template <typename Transfer>
    esp_err_t transfer(uint8_t addr, Transfer &&run, Timing *timing = nullptr) {
        const int64_t requested = timing ? esp_timer_get_time() : 0;
        Lock lock(_mutex);
        i2c_master_dev_handle_t device;
        esp_err_t err = deviceAt(addr, device);
        if (err != ESP_OK) return err;
        const int64_t start = esp_timer_get_time();
        err = run(device);
        if (timing) *timing = {start - requested, esp_timer_get_time() - start};
        if (err != ESP_OK) afterFailure(addr, start, err);
        return err;
    }

    /**
     * The driver fails a NACK and a timeout alike, with ESP_ERR_INVALID_STATE, so the bus tells them apart itself: a
     * timeout leaves a line held low or spends the transfer's whole time, which a device that does not acknowledge
     * does not. The driver waits whole ticks, so a timeout can end a tick short of TIMEOUT_MS.
     *
     * A device cut off mid-read, by a reset of the ESP32 or a glitch, holds SDA low until it has clocked out its byte.
     * The driver's own bus clear does not reach it on the ESP32-S3: it starts the hardware's nine SCL pulses and
     * resets the whole peripheral straight after. So the bus clocks the device free on the pins itself.
     */
    void afterFailure(uint8_t addr, int64_t startUs, esp_err_t err) {
        const int64_t now = esp_timer_get_time();
        const bool spentTimeout = now - startUs >= (TIMEOUT_MS - portTICK_PERIOD_MS) * 1000LL;
        const bool sdaHeld = gpio_get_level(_sda) == 0;
        const bool sclHeld = gpio_get_level(_scl) == 0;
        ErrorCounts &counts = _errors[addr];
        if (err == ESP_ERR_TIMEOUT || spentTimeout || sdaHeld || sclHeld) counts.timeouts++;
        else counts.nacks++;
        // A held SCL is a device stretching the clock, or one without power clamping both lines: no pulses free it.
        if (sdaHeld && !sclHeld && now >= _nextRecoveryUs) {
            _nextRecoveryUs = now + RECOVERY_INTERVAL_US;
            recover();
        }
    }

    // Up to nine SCL pulses, until the device lets SDA go, then a STOP: the standard I2C bus clear, at 100 kHz.
    void recover() {
        const gpio_num_t sda = _sda, scl = _scl;
        const uint32_t freq = _freq;
        const i2c_port_t port = _port;
        end();
        constexpr uint32_t HALF_PERIOD_US = 5;
        gpio_set_level(sda, 1);
        gpio_set_level(scl, 1);
        gpio_set_direction(sda, GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_set_direction(scl, GPIO_MODE_INPUT_OUTPUT_OD);
        for (int pulse = 0; pulse < 9 && gpio_get_level(sda) == 0; pulse++) {
            gpio_set_level(scl, 0);
            esp_rom_delay_us(HALF_PERIOD_US);
            gpio_set_level(scl, 1);
            esp_rom_delay_us(HALF_PERIOD_US);
        }
        gpio_set_level(scl, 0);
        gpio_set_level(sda, 0);
        esp_rom_delay_us(HALF_PERIOD_US);
        gpio_set_level(scl, 1);
        esp_rom_delay_us(HALF_PERIOD_US);
        gpio_set_level(sda, 1);
        esp_rom_delay_us(HALF_PERIOD_US);
        _recoveries++;
        if (begin(sda, scl, freq, port) != ESP_OK) ESP_LOGE(TAG, "The bus did not start again after a recovery");
    }

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
        if (err != ESP_OK) return err;
        _devices[addr] = device;
        _errors.try_emplace(addr);
        return ESP_OK;
    }
};
