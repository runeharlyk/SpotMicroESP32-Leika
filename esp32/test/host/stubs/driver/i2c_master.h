#pragma once
// Host fake of ESP-IDF's I2C master driver: records the devices and buses it hands out, and flags any
// use of a handle that was already freed - what a race between two tasks on the bus would do.
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <vector>

typedef int i2c_port_t;
#define I2C_NUM_0 0
#define I2C_CLK_SRC_DEFAULT 0
#define I2C_ADDR_BIT_LEN_7 0

struct FakeBus {
    gpio_num_t sda;
    gpio_num_t scl;
};
struct FakeDevice {
    uint8_t address;
    const FakeBus *bus;
};
typedef FakeBus *i2c_master_bus_handle_t;
typedef FakeDevice *i2c_master_dev_handle_t;

struct i2c_master_bus_config_t {
    i2c_port_t i2c_port;
    gpio_num_t sda_io_num;
    gpio_num_t scl_io_num;
    int clk_source;
    uint32_t glitch_ignore_cnt;
    struct {
        uint32_t enable_internal_pullup;
    } flags;
};

struct i2c_device_config_t {
    int dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
};

namespace fake_i2c {
inline std::mutex mutex;
inline std::set<const void *> live;
inline std::atomic<int> devicesAdded {0};
inline std::atomic<int> staleUses {0};
inline std::atomic<int> transfers {0};
// The clock the most recently added device was given.
inline std::atomic<uint32_t> deviceSpeedHz {0};
inline std::vector<std::vector<uint8_t>> written;
// What each device's registers hold: reads return them, writes change them, as a register-mapped chip does.
inline std::map<uint8_t, std::array<uint8_t, 256>> registers;
// Devices whose registers are 16 bits wide, as the INA219's: register r is bytes 2r and 2r + 1 of `registers`.
inline std::set<uint8_t> wordAddressed;
// Devices that do not acknowledge their address, as a board without power does not.
inline std::set<uint8_t> absent;
// Transfers each device was sent, answered or not.
inline std::map<uint8_t, int> attempts;
// How long a failed transfer takes on the fake clock: a NACK ends at once, a timeout spends the transfer's whole time.
inline int64_t failureUs = 0;
// How long an answered transfer takes on the fake clock.
inline int64_t transferUs = 0;
// Runs at the start of every transfer to a device, with the bus held: a test acts as another task meanwhile.
inline std::function<void(uint8_t address)> duringTransfer;

inline bool isLive(const void *handle) {
    std::lock_guard<std::mutex> lock(mutex);
    return live.count(handle) != 0;
}
inline void reset() {
    std::lock_guard<std::mutex> lock(mutex);
    fake_gpio::reset();
    live.clear();
    written.clear();
    registers.clear();
    absent.clear();
    wordAddressed.clear();
    attempts.clear();
    failureUs = transferUs = 0;
    duringTransfer = nullptr;
    devicesAdded = staleUses = transfers = 0;
    deviceSpeedHz = 0;
}
} // namespace fake_i2c

inline esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *config, i2c_master_bus_handle_t *bus) {
    *bus = new FakeBus {config->sda_io_num, config->scl_io_num};
    std::lock_guard<std::mutex> lock(fake_i2c::mutex);
    fake_i2c::live.insert(*bus);
    return ESP_OK;
}

inline esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus) {
    {
        std::lock_guard<std::mutex> lock(fake_i2c::mutex);
        fake_i2c::live.erase(bus);
    }
    delete bus;
    return ESP_OK;
}

inline esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *config,
                                           i2c_master_dev_handle_t *device) {
    if (!fake_i2c::isLive(bus)) fake_i2c::staleUses++;
    *device = new FakeDevice {static_cast<uint8_t>(config->device_address), bus};
    fake_i2c::devicesAdded++;
    fake_i2c::deviceSpeedHz = config->scl_speed_hz;
    std::lock_guard<std::mutex> lock(fake_i2c::mutex);
    fake_i2c::live.insert(*device);
    return ESP_OK;
}

inline esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device) {
    {
        std::lock_guard<std::mutex> lock(fake_i2c::mutex);
        fake_i2c::live.erase(device);
    }
    delete device;
    return ESP_OK;
}

inline esp_err_t fakeTransfer(const void *handle) {
    if (!fake_i2c::isLive(handle)) {
        fake_i2c::staleUses++;
        return ESP_FAIL;
    }
    fake_i2c::transfers++;
    return ESP_OK;
}

// The driver fails a transfer the device does not acknowledge with ESP_ERR_INVALID_STATE, as it does one on a bus
// with a line held low.
inline esp_err_t fakeDeviceTransfer(i2c_master_dev_handle_t device) {
    esp_err_t result = fakeTransfer(device);
    if (result != ESP_OK) return result;
    if (fake_i2c::duringTransfer) fake_i2c::duringTransfer(device->address);
    std::lock_guard<std::mutex> lock(fake_i2c::mutex);
    fake_i2c::attempts[device->address]++;
    const bool held = gpio_get_level(device->bus->sda) == 0 || gpio_get_level(device->bus->scl) == 0;
    if (!held && !fake_i2c::absent.count(device->address)) {
        fake_timer::nowUs += fake_i2c::transferUs;
        return ESP_OK;
    }
    fake_timer::nowUs += fake_i2c::failureUs;
    return ESP_ERR_INVALID_STATE;
}

inline esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t *, size_t, int) {
    return fakeDeviceTransfer(device);
}

struct i2c_master_transmit_multi_buffer_info_t {
    const uint8_t *write_buffer;
    size_t buffer_size;
};

// Records what one transaction put on the wire: the parts back to back, as the hardware sends them.
inline esp_err_t i2c_master_multi_buffer_transmit(i2c_master_dev_handle_t device,
                                                  i2c_master_transmit_multi_buffer_info_t *parts, size_t count, int) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < count; i++)
        bytes.insert(bytes.end(), parts[i].write_buffer, parts[i].write_buffer + parts[i].buffer_size);
    {
        std::lock_guard<std::mutex> lock(fake_i2c::mutex);
        fake_i2c::written.push_back(bytes);
    }
    esp_err_t result = fakeDeviceTransfer(device);
    // Only a live device is read: a freed one is what the bus tests catch, not something to touch.
    if (result != ESP_OK || bytes.empty()) return result;
    std::lock_guard<std::mutex> lock(fake_i2c::mutex);
    auto &chip = fake_i2c::registers[device->address];
    const size_t first = fake_i2c::wordAddressed.count(device->address) ? bytes[0] * 2 : bytes[0];
    for (size_t i = 1; i < bytes.size(); i++) chip[(first + i - 1) & 0xFF] = bytes[i];
    return result;
}

inline esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *write, size_t, uint8_t *read,
                                             size_t length, int) {
    esp_err_t result = fakeDeviceTransfer(device);
    if (result != ESP_OK) return result;
    std::lock_guard<std::mutex> lock(fake_i2c::mutex);
    auto &chip = fake_i2c::registers[device->address];
    const size_t first = fake_i2c::wordAddressed.count(device->address) ? write[0] * 2 : write[0];
    for (size_t i = 0; i < length; i++) read[i] = chip[(first + i) & 0xFF];
    return ESP_OK;
}

inline esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int) {
    esp_err_t result = fakeTransfer(bus);
    if (result != ESP_OK) return result;
    std::lock_guard<std::mutex> lock(fake_i2c::mutex);
    return fake_i2c::absent.count(address) ? ESP_ERR_NOT_FOUND : ESP_OK;
}
