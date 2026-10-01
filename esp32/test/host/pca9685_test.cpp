// Host test of the PCA9685 driver's start-up against the register-serving I2C fake.
#include <cstdio>
#include <peripherals/drivers/pca9685.h>

static int failures = 0;

#define CHECK(condition)                                                         do {                                                                             if (!(condition)) {                                                              std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);             failures++;                                                              }                                                                        } while (0)

constexpr uint8_t MODE1 = 0x00;
constexpr uint8_t MODE1_SLEEP = 0x10;
constexpr uint8_t ALL_LED_OFF_H = 0xFD;
constexpr uint8_t FULL_OFF = 0x10;
constexpr uint8_t PRE_SCALE = 0xFE;
constexpr uint32_t OSCILLATOR_HZ = 27000000;

// A chip that slept with the last pose in its channels, as it does after a deactivate and an ESP32 reset:
// RESTART pending, asleep, auto-increment.
static void startWithAStaleSleepingChip() {
    fake_i2c::reset();
    I2CBus::instance().begin(21, 22);
    fake_i2c::registers[0x40][MODE1] = 0x80 | MODE1_SLEEP | 0x20;
}

// Waking the chip restarts its channels with the pose they held, so the legs would jump before the controller
// has said anything: every MODE1 write while starting keeps it asleep.
static void startingNeverWakesTheChip() {
    startWithAStaleSleepingChip();
    PCA9685Driver pca;
    CHECK(pca.begin(OSCILLATOR_HZ, 50));
    int mode1Writes = 0;
    for (const auto &bytes : fake_i2c::written) {
        if (bytes.size() != 2 || bytes[0] != MODE1) continue;
        mode1Writes++;
        CHECK(bytes[1] & MODE1_SLEEP);
    }
    CHECK(mode1Writes > 0);
    CHECK(fake_i2c::registers[0x40][MODE1] & MODE1_SLEEP);
    I2CBus::instance().end();
}

// The datasheet's orderly shutdown: every channel fully off before the chip sleeps, so a later wake starts from
// nothing rather than the pose before the reset.
static void startingTurnsEveryChannelOffBeforeSleeping() {
    startWithAStaleSleepingChip();
    PCA9685Driver pca;
    CHECK(pca.begin(OSCILLATOR_HZ, 50));
    const auto &writes = fake_i2c::written;
    size_t fullOff = writes.size(), firstSleep = writes.size();
    for (size_t i = 0; i < writes.size(); i++) {
        if (writes[i].size() == 2 && writes[i][0] == ALL_LED_OFF_H && (writes[i][1] & FULL_OFF) && fullOff == writes.size())
            fullOff = i;
        if (writes[i].size() == 2 && writes[i][0] == MODE1 && firstSleep == writes.size()) firstSleep = i;
    }
    CHECK(fullOff < writes.size());
    CHECK(fullOff < firstSleep);
    I2CBus::instance().end();
}

// 27 MHz / (4096 * 50 Hz) - 1 rounds to 131: one prescale, for the board's real oscillator.
static void theFrequencyComesFromTheGivenOscillator() {
    startWithAStaleSleepingChip();
    PCA9685Driver pca;
    CHECK(pca.begin(OSCILLATOR_HZ, 50));
    int prescaleWrites = 0;
    for (const auto &bytes : fake_i2c::written)
        if (bytes.size() == 2 && bytes[0] == PRE_SCALE) prescaleWrites++;
    CHECK(prescaleWrites == 1);
    CHECK(fake_i2c::registers[0x40][PRE_SCALE] == 131);
    I2CBus::instance().end();
}

int main() {
    startingNeverWakesTheChip();
    startingTurnsEveryChannelOffBeforeSleeping();
    theFrequencyComesFromTheGivenOscillator();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
