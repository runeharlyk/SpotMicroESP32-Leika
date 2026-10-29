// Host test of peripherals/i2c_bus.h against a fake I2C driver (stubs/driver/i2c_master.h), built and
// run by test_host_programs.py.
#include <cstdio>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
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

static void aRegisterWriteSendsTheRegisterThenTheDataInOneTransaction() {
    fake_i2c::reset();
    I2CBus &bus = I2CBus::instance();
    bus.begin(21, 22);
    const uint8_t pwm[4] = {0x00, 0x00, 0x34, 0x01};
    CHECK(bus.writeReg(PCA9685, 0x06, pwm, 4) == ESP_OK);
    CHECK(bus.writeReg(MPU6050, 0x6B, nullptr, 0) == ESP_OK);
    CHECK(fake_i2c::written.size() == 2);
    CHECK((fake_i2c::written[0] == std::vector<uint8_t> {0x06, 0x00, 0x00, 0x34, 0x01}));
    CHECK((fake_i2c::written[1] == std::vector<uint8_t> {0x6B}));
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

// A scan holds the bus one probe at a time, so a restart can come between two probes: none may use the
// bus the restart freed.
static void aScanNeverProbesAFreedBusWhileTheBusRestarts() {
    fake_i2c::reset();
    I2CBus &bus = I2CBus::instance();
    bus.begin(21, 22);
    std::atomic<bool> running {true};
    std::thread scanner([&] {
        while (running) bus.scan();
    });
    for (int restart = 0; restart < 300 || fake_i2c::transfers < 1000; restart++) {
        bus.end();
        bus.begin(21, 22);
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        if (restart > 100000) break;
    }
    running = false;
    scanner.join();
    CHECK(fake_i2c::staleUses == 0);
    CHECK(fake_i2c::transfers >= 1000);
    bus.end();
}

// Every sensor here (MPU6050, HMC5883L, PAJ7620) is rated for 400 kHz; robots still store the old 1 MHz default.
static void theBusRunsNoFasterThanItsSlowestDevices() {
    fake_i2c::reset();
    I2CBus &bus = I2CBus::instance();
    uint8_t value = 0;
    bus.begin(21, 22, 1000000);
    CHECK(bus.writeReg(MPU6050, 0x6b, &value, 1) == ESP_OK);
    CHECK(bus.freq() == 400000 && fake_i2c::deviceSpeedHz == 400000);
    bus.end();

    bus.begin(21, 22, 100000);
    CHECK(bus.writeReg(MPU6050, 0x6b, &value, 1) == ESP_OK);
    CHECK(bus.freq() == 100000 && fake_i2c::deviceSpeedHz == 100000);
    bus.end();
}

static I2CBus::DeviceErrors errorsAt(uint8_t address) {
    for (const auto &device : I2CBus::instance().errors())
        if (device.address == address) return device;
    return {0, 0, 0};
}

// A servo board without power does not acknowledge, on a free bus: each failed write is a NACK, and a device that
// answers is listed with none.
static void aDeviceThatDoesNotAnswerCountsNacks() {
    fake_i2c::reset();
    I2CBus &bus = I2CBus::instance();
    bus.begin(21, 22);
    const auto servosBefore = errorsAt(PCA9685), imuBefore = errorsAt(MPU6050);
    fake_i2c::absent = {PCA9685};
    // The longest write here, all 16 channels at 100 kHz, takes about 4.5 ms before a last byte's NACK.
    fake_i2c::failureUs = 4500;
    uint8_t value[2] = {0, 0};
    for (int tick = 0; tick < 5; tick++) {
        CHECK(bus.writeReg(PCA9685, 0x06, value, 2) != ESP_OK);
        CHECK(bus.readReg(MPU6050, 0x3B, value, 2) == ESP_OK);
    }
    CHECK(errorsAt(PCA9685).address == PCA9685);
    CHECK(errorsAt(PCA9685).nacks == servosBefore.nacks + 5);
    CHECK(errorsAt(PCA9685).timeouts == servosBefore.timeouts);
    CHECK(errorsAt(MPU6050).address == MPU6050);
    CHECK(errorsAt(MPU6050).nacks == imuBefore.nacks && errorsAt(MPU6050).timeouts == imuBefore.timeouts);
    bus.end();
}

// A device holding SDA low, or a transfer that spends its whole timeout, is a timeout rather than a NACK.
static void aHeldLineOrASpentTimeoutCountsATimeout() {
    fake_i2c::reset();
    I2CBus &bus = I2CBus::instance();
    bus.begin(21, 22);
    const auto before = errorsAt(MPU6050);
    fake_i2c::absent = {MPU6050};
    uint8_t value = 0;

    fake_gpio::low = {21};
    CHECK(bus.readReg(MPU6050, 0x75, &value, 1) != ESP_OK);
    fake_gpio::low.clear();
    CHECK(errorsAt(MPU6050).timeouts == before.timeouts + 1);

    // The driver waits whole 10 ms ticks: its 20 ms timeout can end after 10.
    fake_i2c::failureUs = 10000;
    CHECK(bus.readReg(MPU6050, 0x75, &value, 1) != ESP_OK);
    CHECK(errorsAt(MPU6050).timeouts == before.timeouts + 2);
    CHECK(errorsAt(MPU6050).nacks == before.nacks);
    bus.end();
}

// Saving new pins restarts the bus; the counts are since boot, so they stay.
static void theCountsOutliveARestartOfTheBus() {
    fake_i2c::reset();
    I2CBus &bus = I2CBus::instance();
    bus.begin(21, 22);
    fake_i2c::absent = {PCA9685};
    const auto before = errorsAt(PCA9685);
    uint8_t value = 0;
    CHECK(bus.writeReg(PCA9685, 0x00, &value, 1) != ESP_OK);
    bus.end();
    bus.begin(5, 6);
    CHECK(errorsAt(PCA9685).nacks == before.nacks + 1);
    bus.end();
}

// The ESP32 reset while the IMU was sending a byte of zeros: the IMU holds SDA low and every transfer fails, until the
// bus clocks out the rest of the byte and ends the transfer with a STOP.
static void aDeviceHoldingSdaIsClockedFreeAndTheBusWorksAgain() {
    fake_i2c::reset();
    I2CBus &bus = I2CBus::instance();
    bus.begin(21, 22);
    fake_timer::nowUs += 2000000;
    const uint32_t before = bus.recoveries();
    fake_gpio::low = {21};
    fake_gpio::stuck = {21, 22, 4};
    uint8_t value = 0;
    CHECK(bus.readReg(MPU6050, 0x75, &value, 1) != ESP_OK);
    CHECK(bus.recoveries() == before + 1);
    // Four pulses free it, and the STOP needs the clock high once more.
    CHECK(fake_gpio::clockPulses == 5);
    CHECK(fake_gpio::stops == 1);
    CHECK(bus.readReg(MPU6050, 0x75, &value, 1) == ESP_OK);
    CHECK(fake_i2c::staleUses == 0);
    bus.end();
}

// A device that never lets go gets nine pulses, and no more than one recovery a second however often transfers fail.
static void aDeviceThatNeverLetsGoIsRecoveredAtMostOnceASecond() {
    fake_i2c::reset();
    I2CBus &bus = I2CBus::instance();
    bus.begin(21, 22);
    fake_timer::nowUs += 2000000;
    const uint32_t before = bus.recoveries();
    fake_gpio::low = {21};
    fake_gpio::stuck = {21, 22, 0};
    fake_i2c::failureUs = 10000;
    uint8_t value = 0;
    for (int tick = 0; tick < 150; tick++) CHECK(bus.readReg(MPU6050, 0x75, &value, 1) != ESP_OK);
    CHECK(bus.recoveries() == before + 2);
    CHECK(fake_gpio::clockPulses == 2 * (9 + 1));
    bus.end();
}

// SCL held low is a device stretching the clock, or one without power clamping the lines: pulses cannot free it.
static void aHeldClockIsNotRecovered() {
    fake_i2c::reset();
    I2CBus &bus = I2CBus::instance();
    bus.begin(21, 22);
    fake_timer::nowUs += 2000000;
    const uint32_t before = bus.recoveries();
    fake_gpio::low = {21, 22};
    uint8_t value = 0;
    CHECK(bus.readReg(MPU6050, 0x75, &value, 1) != ESP_OK);
    CHECK(bus.recoveries() == before);
    bus.end();
}

// The control task's servo write times its wait for the bus apart from its transfer: while the sensor task's IMU read
// holds the bus for 3 ms, the write waits that long plus the read's own transfer, and then spends only its own.
static void aWriteTimesItsWaitForTheBusApartFromItsTransfer() {
    fake_i2c::reset();
    fake_timer::nowUs = 0;
    I2CBus &bus = I2CBus::instance();
    bus.begin(21, 22);
    fake_i2c::transferUs = 1100;
    const uint8_t pwm[48] = {};
    I2CBus::Timing alone;
    CHECK(bus.writeReg(PCA9685, 0x06, pwm, sizeof(pwm), &alone) == ESP_OK);
    CHECK(alone.waitUs == 0 && alone.transferUs == 1100);

    I2CBus::Timing waited;
    std::thread control;
    fake_i2c::duringTransfer = [&](uint8_t address) {
        if (address != MPU6050) return;
        control = std::thread([&] { bus.writeReg(PCA9685, 0x06, pwm, sizeof(pwm), &waited); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // the write now waits for the bus
        fake_timer::nowUs += 3000;
    };
    uint8_t imu[14];
    CHECK(bus.readReg(MPU6050, 0x3B, imu, sizeof(imu)) == ESP_OK);
    control.join();
    fake_i2c::duringTransfer = nullptr;
    CHECK(waited.waitUs == 3000 + 1100);
    CHECK(waited.transferUs == 1100);
    bus.end();
}

int main() {
    aDeviceHoldingSdaIsClockedFreeAndTheBusWorksAgain();
    aDeviceThatNeverLetsGoIsRecoveredAtMostOnceASecond();
    aHeldClockIsNotRecovered();
    aDeviceThatDoesNotAnswerCountsNacks();
    aHeldLineOrASpentTimeoutCountsATimeout();
    theCountsOutliveARestartOfTheBus();
    devicesAreCreatedOnceNotPerTransfer();
    theBusRunsNoFasterThanItsSlowestDevices();
    aStoppedBusRefusesTransfersAndStartsAgainClean();
    aRegisterWriteSendsTheRegisterThenTheDataInOneTransaction();
    transfersNeverUseAFreedHandleWhileTheBusRestarts();
    aScanNeverProbesAFreedBusWhileTheBusRestarts();
    aWriteTimesItsWaitForTheBusApartFromItsTransfer();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
