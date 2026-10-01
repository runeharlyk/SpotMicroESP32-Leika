#pragma once
// Host fake of ESP-IDF's I2C master driver: records the devices and buses it hands out, and flags any
// use of a handle that was already freed - what a race between two tasks on the bus would do.
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <set>
#include <vector>

typedef int i2c_port_t;
#define I2C_NUM_0 0
typedef int gpio_num_t;
#define GPIO_NUM_NC -1
#define I2C_CLK_SRC_DEFAULT 0
#define I2C_ADDR_BIT_LEN_7 0

struct FakeBus {};
struct FakeDevice {
    uint8_t address;
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
inline std::vector<std::vector<uint8_t>> written;

inline bool isLive(const void *handle) {
    std::lock_guard<std::mutex> lock(mutex);
    return live.count(handle) != 0;
}
inline void reset() {
    std::lock_guard<std::mutex> lock(mutex);
    live.clear();
    written.clear();
    devicesAdded = staleUses = transfers = 0;
}
} // namespace fake_i2c

inline esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *, i2c_master_bus_handle_t *bus) {
    *bus = new FakeBus();
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
    *device = new FakeDevice {static_cast<uint8_t>(config->device_address)};
    fake_i2c::devicesAdded++;
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

inline esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t *, size_t, int) {
    return fakeTransfer(device);
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
    return fakeTransfer(device);
}

inline esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *, size_t, uint8_t *,
                                             size_t, int) {
    return fakeTransfer(device);
}

inline esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t, int) { return fakeTransfer(bus); }
