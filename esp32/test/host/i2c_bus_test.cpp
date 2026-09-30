// Host test of peripherals/i2c_bus.h against a fake I2C driver (stubs/driver/i2c_master.h), built and
// run by test_host_programs.py.
#include <cstdio>
#include <chrono>
#include <thread>
#include <atomic>
#include <peripherals/i2c_bus.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

constexpr uint8_t PCA9685 = 0x40;
constexpr uint8_t MPU6050 = 0x68;

static void devicesAreCreatedOnceNotPerTransfer() {
    fake_i2c::reset();
    I2CBus &bus = I2CBus::instance();
    bus.begin(21, 22);
    uint8_t value[2] = {0, 0};
    // The control loop alternates the servo board and the IMU every tick.
    for (int tick = 0; tick < 100; tick++) {
        CHECK(bus.writeReg(PCA9685, 0x06, value, 2) == ESP_OK);
        CHECK(bus.readReg(MPU6050, 0x3B, value, 2) == ESP_OK);
    }
    CHECK(fake_i2c::devicesAdded == 2);
    bus.end();
}

static void aStoppedBusRefusesTransfersAndStartsAgainClean() {
    fake_i2c::reset();
    I2CBus &bus = I2CBus::instance();
    bus.begin(21, 22);
    uint8_t value = 0;
    bus.readReg(MPU6050, 0x75, &value, 1);
    bus.end();
    CHECK(bus.readReg(MPU6050, 0x75, &value, 1) == ESP_ERR_INVALID_STATE);
    bus.begin(5, 6);
    CHECK(bus.readReg(MPU6050, 0x75, &value, 1) == ESP_OK);
    CHECK(fake_i2c::staleUses == 0);
    bus.end();
}

// Two tasks transfer while a third restarts the bus (new pins saved from the app): no transfer may
// ever use a device or bus the restart freed.
static void transfersNeverUseAFreedHandleWhileTheBusRestarts() {
    fake_i2c::reset();
    I2CBus &bus = I2CBus::instance();
    bus.begin(21, 22);
    std::atomic<bool> running {true};
    auto transfer = [&](uint8_t address) {
        uint8_t value[2];
        while (running) bus.readReg(address, 0x00, value, 2);
    };
    std::thread control(transfer, PCA9685);
    std::thread sensors(transfer, MPU6050);
    // A lock is not fair: without the pause the restarts would starve the transfers of any turn.
    for (int restart = 0; restart < 300 || fake_i2c::transfers < 1000; restart++) {
        bus.end();
        bus.begin(21, 22);
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        if (restart > 100000) break;
    }
    running = false;
    control.join();
    sensors.join();
    CHECK(fake_i2c::staleUses == 0);
    CHECK(fake_i2c::transfers >= 1000);
    bus.end();
}

int main() {
    devicesAreCreatedOnceNotPerTransfer();
    aStoppedBusRefusesTransfersAndStartsAgainClean();
    transfersNeverUseAFreedHandleWhileTheBusRestarts();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
