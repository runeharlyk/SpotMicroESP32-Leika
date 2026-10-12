// Host test of the servo rail's INA219 driver (peripherals/drivers/ina219.h) against the register-serving I2C fake, of
// the cut-off and stall flags (peripherals/power_guard.h) and of the power sensor settings.
#include <cmath>
#include <cstdio>
#include <limits>
#include <peripherals/drivers/ina219.h>
#include <peripherals/power_guard.h>
#include <settings/peripherals_settings.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static bool near(float a, float b, float tolerance) { return std::fabs(a - b) < tolerance; }

constexpr uint8_t ADDR = 0x41;

static void putWord(uint8_t reg, uint16_t value) {
    fake_i2c::registers[ADDR][reg * 2] = static_cast<uint8_t>(value >> 8);
    fake_i2c::registers[ADDR][reg * 2 + 1] = static_cast<uint8_t>(value & 0xFF);
}

static uint16_t word(uint8_t reg) {
    return static_cast<uint16_t>((fake_i2c::registers[ADDR][reg * 2] << 8) | fake_i2c::registers[ADDR][reg * 2 + 1]);
}

// An INA219 as it comes out of reset: the default configuration, no calibration, so no current.
static void powerOnChip() {
    fake_i2c::reset();
    fake_i2c::wordAddressed.insert(ADDR);
    putWord(0x00, 0x399F);
    I2CBus::instance().begin(21, 22);
}

// The chip's own current register: shunt register times calibration over 4096 (datasheet equation 4).
static int16_t chipCurrent(int16_t shuntRegister, uint16_t calibration) {
    return static_cast<int16_t>(static_cast<int32_t>(shuntRegister) * calibration / 4096);
}

static void calibrationFollowsTheDatasheet() {
    // 3.2 A through 0.1 Ohm: an LSB of 3.2 A / 2^15, trunc(0.04096 / (LSB x 0.1)) = 4194, the +-320 mV range.
    const Ina219Calibration wide = Ina219Calibration::forShunt(0.1f, 3.2f);
    CHECK(near(wide.currentLsbA, 3.2f / 32768, 1e-9f));
    CHECK(wide.calibration == 4194);
    CHECK(wide.pga == 3);

    // The narrowest range that holds the largest current across the shunt.
    CHECK(Ina219Calibration::forShunt(0.1f, 0.3f).pga == 0); // 30 mV of 40
    CHECK(Ina219Calibration::forShunt(0.1f, 0.6f).pga == 1); // 60 mV of 80
    CHECK(Ina219Calibration::forShunt(0.1f, 1.0f).pga == 2); // 100 mV of 160
    CHECK(Ina219Calibration::forShunt(0.01f, 10.0f).pga == 2);

    // A small current through a small shunt would need more than 16 bits: the LSB grows instead, and FS0 stays clear.
    const Ina219Calibration fine = Ina219Calibration::forShunt(0.1f, 0.05f);
    CHECK(fine.calibration == 0xFFFE);
    CHECK(near(fine.currentLsbA, 0.04096f / (0xFFFE * 0.1f), 1e-10f));
    CHECK((Ina219Calibration::forShunt(0.05f, 2.0f).calibration & 1) == 0);

    // Whatever the calibration, the chip's current is the shunt voltage over the shunt.
    for (const auto &[ohms, amps] : {std::pair {0.1f, 3.2f}, {0.1f, 0.05f}, {0.01f, 10.0f}, {0.05f, 2.0f}}) {
        const Ina219Calibration cal = Ina219Calibration::forShunt(ohms, amps);
        const int16_t shunt = static_cast<int16_t>(std::lround(0.8f * amps * ohms / 10e-6f)); // 80 % of the range
        const PowerReading reading = ina219Reading(0, shunt, chipCurrent(shunt, cal.calibration), cal.currentLsbA);
        CHECK(near(reading.current, shunt * 10e-6f / ohms, amps * 0.002f));
    }
}

static void registersConvertToSiUnits() {
    // 7.40 V is 1850 steps of 4 mV in bits 15-3; the conversion-ready and overflow bits below them are not volts.
    const PowerReading reading = ina219Reading((1850 << 3) | 0x3, 15000, 1500, 1e-4f);
    CHECK(near(reading.busVoltage, 7.40f, 1e-5f));
    CHECK(near(reading.shuntVoltage, 0.150f, 1e-6f));
    CHECK(near(reading.current, 0.150f, 1e-6f));
    CHECK(near(reading.power, 7.40f * 0.150f, 1e-5f));

    // Current flowing back into the supply reads negative, as the registers are two's complement.
    const PowerReading back = ina219Reading(1850 << 3, -320, -32, 1e-4f);
    CHECK(near(back.shuntVoltage, -0.0032f, 1e-7f));
    CHECK(near(back.current, -0.0032f, 1e-7f));
    CHECK(back.power < 0);
}

static void anAbsentChipIsNotStarted() {
    powerOnChip();
    fake_i2c::absent.insert(ADDR);
    INA219Driver ina;
    ina.setAddress(ADDR);
    CHECK(!ina.identify());
    CHECK(!ina.begin(0.1f, 3.2f));
    PowerReading reading;
    CHECK(!ina.read(reading));
    CHECK(fake_i2c::written.empty()); // nothing configured on a device that is not there
    I2CBus::instance().end();
}

static void anotherChipIsNotTakenForOne() {
    powerOnChip();
    putWord(0x00, 0x7FFF); // bit 14 set, which an INA219 never reads
    INA219Driver ina;
    ina.setAddress(ADDR);
    CHECK(!ina.identify());
    CHECK(!ina.begin(0.1f, 3.2f));
    CHECK(fake_i2c::written.empty());
    I2CBus::instance().end();
}

static void startsAndReadsTheRail() {
    powerOnChip();
    INA219Driver ina;
    ina.setAddress(ADDR);
    CHECK(ina.identify());
    CHECK(fake_i2c::written.empty()); // identifying leaves the chip as it is
    CHECK(ina.begin(0.1f, 3.2f));
    // 32 V range, +-320 mV, bus averaged over 16 samples, shunt over 128, both continuous.
    CHECK(word(0x00) == ((1 << 13) | (3 << 11) | (0xC << 7) | (0xF << 3) | 0x7));
    CHECK(word(0x05) == 4194);

    putWord(0x02, (1550 << 3) | 0x2); // 6.20 V
    putWord(0x01, 15000);             // 150 mV: 1.5 A through 0.1 Ohm
    putWord(0x04, static_cast<uint16_t>(chipCurrent(15000, 4194)));
    PowerReading reading;
    CHECK(ina.read(reading));
    CHECK(near(reading.busVoltage, 6.20f, 1e-5f));
    CHECK(near(reading.shuntVoltage, 0.150f, 1e-6f));
    CHECK(near(reading.current, 1.5f, 0.002f));
    CHECK(near(reading.power, 6.20f * 1.5f, 0.02f));
    I2CBus::instance().end();
}

static void aChipThatResetIsConfiguredAgain() {
    powerOnChip();
    INA219Driver ina;
    ina.setAddress(ADDR);
    CHECK(ina.begin(0.1f, 3.2f));
    const uint16_t configured = word(0x00);
    putWord(0x00, 0x399F); // a dip on its own supply reset it
    putWord(0x05, 0);
    PowerReading reading;
    CHECK(ina.read(reading));
    CHECK(word(0x00) == configured);
    CHECK(word(0x05) == 4194);
    I2CBus::instance().end();
}

static void aChipThatStopsAnsweringFailsTheRead() {
    powerOnChip();
    INA219Driver ina;
    ina.setAddress(ADDR);
    CHECK(ina.begin(0.1f, 3.2f));
    fake_i2c::absent.insert(ADDR);
    PowerReading reading;
    reading.busVoltage = 7.0f;
    CHECK(!ina.read(reading));
    CHECK(reading.busVoltage == 7.0f); // a failed read leaves the last reading alone
    I2CBus::instance().end();
}

static void theCutOffWaitsTwoSeconds() {
    PowerGuard guard;
    guard.configure({.cutoffVoltage = 6.4f, .stallCurrent = 0, .stallMs = 0});
    guard.update(0, 7.0f, 1.0f);
    CHECK(!guard.lowVoltage());
    guard.update(100, 6.3f, 1.0f);
    guard.update(2099, 6.3f, 1.0f);
    CHECK(!guard.lowVoltage());
    guard.update(2100, 6.3f, 1.0f);
    CHECK(guard.lowVoltage());
}

static void aSagThatRecoversIsNotCutOff() {
    PowerGuard guard;
    guard.configure({.cutoffVoltage = 6.4f, .stallCurrent = 0, .stallMs = 0});
    guard.update(0, 6.0f, 3.0f);    // a step sags the rail
    guard.update(1500, 6.7f, 0.5f); // and it recovers past the band
    guard.update(1600, 6.0f, 3.0f); // the next step starts the hold afresh
    guard.update(3500, 6.0f, 3.0f);
    CHECK(!guard.lowVoltage());
    guard.update(3600, 6.0f, 3.0f);
    CHECK(guard.lowVoltage());
}

static void noiseAroundTheCutOffNeitherRestartsNorClears() {
    PowerGuard guard;
    guard.configure({.cutoffVoltage = 6.4f, .stallCurrent = 0, .stallMs = 0});
    guard.update(0, 6.35f, 1.0f);
    guard.update(1000, 6.55f, 1.0f); // above the cut-off, but within the band
    guard.update(2000, 6.38f, 1.0f);
    CHECK(guard.lowVoltage());
    guard.update(2100, 6.59f, 0.1f); // servos limp: the rail rises, yet not by the band
    CHECK(guard.lowVoltage());
    guard.update(2200, 6.61f, 0.1f);
    CHECK(!guard.lowVoltage());
}

static void noCutOffWhenItIsOff() {
    PowerGuard guard;
    guard.configure({.cutoffVoltage = 0, .stallCurrent = 0, .stallMs = 0});
    guard.update(0, 0.0f, 0.0f);
    guard.update(60000, 0.0f, 0.0f);
    CHECK(!guard.lowVoltage());
    CHECK(!guard.stalled());
}

static void theStallFlagNeedsTheWholeTime() {
    PowerGuard guard;
    guard.configure({.cutoffVoltage = 0, .stallCurrent = 3.0f, .stallMs = 1000});
    guard.update(0, 7.0f, 3.5f);
    guard.update(500, 7.0f, 2.9f); // a spike that ends is not a stall
    guard.update(600, 7.0f, 3.5f);
    guard.update(1599, 7.0f, 3.5f);
    CHECK(!guard.stalled());
    guard.update(1600, 7.0f, 3.5f);
    CHECK(guard.stalled());
    guard.update(1700, 7.0f, 3.0f); // back to the threshold clears it
    CHECK(!guard.stalled());
}

static void aNewConfigurationStartsAfresh() {
    PowerGuard guard;
    guard.configure({.cutoffVoltage = 6.4f, .stallCurrent = 0, .stallMs = 0});
    guard.update(0, 6.0f, 1.0f);
    guard.update(2000, 6.0f, 1.0f);
    CHECK(guard.lowVoltage());
    guard.configure({.cutoffVoltage = 0, .stallCurrent = 0, .stallMs = 0});
    CHECK(!guard.lowVoltage());
    guard.update(2100, 6.0f, 1.0f);
    CHECK(!guard.lowVoltage());
}

static void settingsAreChecked() {
    CHECK(validPowerSettings(powerSensorDefaults()));
    CHECK(powerSensorDefaults().address == 0x41);
    CHECK(powerSensorDefaults().cutoff_voltage == 0);

    const auto with = [](auto change) {
        api_PowerSensorSettings power = powerSensorDefaults();
        change(power);
        return validPowerSettings(power);
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(!with([](auto &p) { p.address = 0x40; })); // the servo board's
    CHECK(!with([](auto &p) { p.address = 0x50; }));
    CHECK(with([](auto &p) { p.address = 0x4F; }));
    CHECK(!with([](auto &p) { p.shunt_ohms = 0; }));
    CHECK(!with([nan](auto &p) { p.shunt_ohms = nan; }));
    CHECK(!with([](auto &p) { p.max_current_a = 0; }));
    CHECK(!with([](auto &p) { p.max_current_a = 3.3f; })); // 0.33 V across 0.1 Ohm
    CHECK(with([](auto &p) { p.shunt_ohms = 0.01f, p.max_current_a = 20.0f; }));
    CHECK(!with([](auto &p) { p.cutoff_voltage = -1; }));
    CHECK(!with([](auto &p) { p.cutoff_voltage = 27; }));
    CHECK(!with([nan](auto &p) { p.cutoff_voltage = nan; }));
    CHECK(!with([](auto &p) { p.stall_current_a = -0.1f; }));
}

static void aSaveWithoutThemKeepsThem() {
    PeripheralsConfiguration stored = PeripheralsConfiguration_defaults();
    stored.power_sensor.cutoff_voltage = 6.4f;

    PeripheralsConfiguration pinsOnly = api_PeripheralSettings_init_zero;
    pinsOnly.sda = stored.sda;
    pinsOnly.scl = stored.scl;
    CHECK(PeripheralsConfiguration_save(pinsOnly, stored) == StateUpdateResult::CHANGED);
    CHECK(stored.has_power_sensor && stored.power_sensor.cutoff_voltage == 6.4f);

    PeripheralsConfiguration bad = stored;
    bad.power_sensor.address = 0x40;
    CHECK(PeripheralsConfiguration_save(bad, stored) == StateUpdateResult::ERROR);
    CHECK(stored.power_sensor.address == 0x41);

    // A file from before the power sensor reads as the defaults.
    PeripheralsConfiguration old = api_PeripheralSettings_init_zero;
    PeripheralsConfiguration read = api_PeripheralSettings_init_zero;
    PeripheralsConfiguration_read(old, read);
    CHECK(read.has_power_sensor && read.power_sensor.address == 0x41 && read.power_sensor.shunt_ohms == 0.1f);
}

int main() {
    calibrationFollowsTheDatasheet();
    registersConvertToSiUnits();
    anAbsentChipIsNotStarted();
    anotherChipIsNotTakenForOne();
    startsAndReadsTheRail();
    aChipThatResetIsConfiguredAgain();
    aChipThatStopsAnsweringFailsTheRead();
    theCutOffWaitsTwoSeconds();
    aSagThatRecoversIsNotCutOff();
    noiseAroundTheCutOffNeitherRestartsNorClears();
    noCutOffWhenItIsOff();
    theStallFlagNeedsTheWholeTime();
    aNewConfigurationStartsAfresh();
    settingsAreChecked();
    aSaveWithoutThemKeepsThem();
    if (failures == 0) std::printf("power sensor tests passed\n");
    return failures == 0 ? 0 : 1;
}
