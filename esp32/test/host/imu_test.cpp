// Host test of peripherals/imu/imu.h and settings/imu_settings.h with scripted drivers.
#include <cmath>
#include <cstdio>
#include <vector>
#include <peripherals/imu/imu.h>
#include <settings/imu_settings.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static bool near(float a, float b, float tolerance) { return std::fabs(a - b) < tolerance; }

// Replays readings; the last one repeats once the script runs out.
class ScriptedImu final : public ImuDriver {
  public:
    std::vector<RawImu> script;
    size_t next = 0;
    bool present = true;
    bool begin() override { return present; }
    bool read(RawImu &raw) override {
        raw = script[next < script.size() ? next++ : script.size() - 1];
        return true;
    }
    const char *name() const override { return "scripted"; }
    uint32_t rateHz() const override { return 200; }
};

class ScriptedMag final : public MagDriver {
  public:
    Vec3 field {20, 0, -40};
    bool present = true;
    int reads = 0;
    bool begin() override { return present; }
    bool read(Vec3 &microtesla) override {
        reads++;
        microtesla = field;
        return true;
    }
    const char *name() const override { return "scripted mag"; }
    uint32_t rateHz() const override { return 75; }
};

static RawImu still(Vec3 gyro = {0, 0, 0}) {
    RawImu raw;
    raw.accel = {0, 0, 9.81f};
    raw.gyro = gyro;
    return raw;
}

static void aStillRobotsGyroBiasIsRemoved() {
    ScriptedImu driver;
    driver.script = {still({0.01f, -0.02f, 0.005f})};
    Imu imu(&driver, nullptr);
    CHECK(imu.begin(0));
    int64_t clock = 0;
    CHECK(imu.estimateGyroBias([&] { return clock += 5000; }));
    ImuSample sample;
    CHECK(imu.update(clock, sample));
    CHECK(near(sample.gyro[0], 0, 1e-5f) && near(sample.gyro[1], 0, 1e-5f) && near(sample.gyro[2], 0, 1e-5f));
}

static void aMovingRobotKeepsThePreviousBias() {
    ScriptedImu driver;
    for (int i = 0; i < 400; i++) driver.script.push_back(still({(i % 2) ? 0.5f : -0.5f, 0, 0}));
    Imu imu(&driver, nullptr);
    CHECK(imu.begin(0));
    int64_t clock = 0;
    CHECK(!imu.estimateGyroBias([&] { return clock += 5000; }));
    ImuSample sample;
    CHECK(imu.update(clock, sample));
    CHECK(near(std::fabs(sample.gyro[0]), 0.5f, 1e-5f));  // nothing subtracted
}

static void theMountingTurnsChipReadingsIntoTheBodyFrame() {
    ScriptedImu driver;
    RawImu raw = still({0.1f, 0.2f, 0.3f});
    raw.accel = {1, 2, 9.81f};
    driver.script = {raw};
    Imu imu(&driver, nullptr);
    CHECK(imu.begin(0));
    ImuConfig config;
    config.mounting = {-1, 0, 0, 0, -1, 0, 0, 0, 1};  // chip turned around on the robot
    imu.configure(config);
    ImuSample sample;
    CHECK(imu.update(5000, sample));
    CHECK(near(sample.accel[0], -1, 1e-5f) && near(sample.accel[1], -2, 1e-5f));
    CHECK(near(sample.gyro[0], -0.1f, 1e-5f) && near(sample.gyro[2], 0.3f, 1e-5f));
}

static void aTiltedRobotIsFoundWithinTheFastStart() {
    ScriptedImu driver;
    RawImu raw = still();
    raw.accel = {0, 4.905f, 8.4957f};  // rolled 30 degrees, left side up: the force pushes up and to the left
    driver.script = {raw};
    ScriptedMag mag;
    mag.field = {20, -20, -34.641f};    // the earth's field (20, 0, -40) as the rolled body sees it
    Imu imu(&driver, &mag);
    CHECK(imu.begin(0));
    ImuSample sample;
    for (int64_t t = 5000; t <= 2000000; t += 5000) CHECK(imu.update(t, sample));
    CHECK(near(sample.rpy[0], 0.5236f, 0.02f));
    CHECK(near(sample.gravity[1], -0.5f, 0.02f));
    CHECK(sample.valid & ImuValid::MAG);
    CHECK(!(sample.valid & ImuValid::YAW_DRIFTS));
    CHECK(mag.reads > 140 && mag.reads < 160);  // 75 Hz over 2 s, not every 200 Hz read
}

static void withoutAMagnetometerYawIsMarkedAsDrifting() {
    ScriptedImu driver;
    driver.script = {still()};
    ScriptedMag mag;
    mag.present = false;
    Imu imu(&driver, &mag);
    CHECK(imu.begin(0));
    ImuSample sample;
    CHECK(imu.update(5000, sample));
    CHECK(sample.valid & ImuValid::ORIENTATION);
    CHECK(sample.valid & ImuValid::YAW_DRIFTS);
    CHECK(!(sample.valid & ImuValid::MAG));
    CHECK(mag.reads == 0);
}

static void anAbsentImuReportsNothing() {
    ScriptedImu driver;
    driver.present = false;
    Imu imu(&driver, nullptr);
    CHECK(!imu.begin(0));
    ImuSample sample;
    CHECK(!imu.update(5000, sample));
    CHECK(sample.valid == 0);
}

// A chip with its own fusion (BNO055) mounted turned 90 degrees on a level robot: the body is level.
static void aChipsOwnFusionIsTurnedIntoTheBodyFrame() {
    ScriptedImu driver;
    RawImu raw = still();
    raw.hasOrientation = true;
    raw.quat = {0.70710678f, 0, 0, 0.70710678f};  // the chip reports a 90 degree yaw
    driver.script = {raw};
    Imu imu(&driver, nullptr);
    CHECK(imu.begin(0));
    ImuConfig config;
    config.mounting = {0, -1, 0, 1, 0, 0, 0, 0, 1};
    imu.configure(config);
    ImuSample sample;
    CHECK(imu.update(5000, sample));
    CHECK(near(sample.rpy[2], 0, 1e-4f));
    CHECK(!(sample.valid & ImuValid::YAW_DRIFTS));
}

static void settingsWithoutImuUseTheDefaults() {
    api_PeripheralSettings stored = api_PeripheralSettings_init_zero;  // a file from before ImuSettings
    const api_ImuSettings settings = effectiveImuSettings(stored);
    CHECK(validImuSettings(settings));
    const ImuConfig config = imuConfigFrom(settings);
    CHECK(config.mounting == IDENTITY3 && config.magAlignment == IDENTITY3 && config.magSoftIron == IDENTITY3);
    CHECK(near(config.fusionGain, 0.1f, 1e-6f));
}

static void onlySensibleImuSettingsAreAccepted() {
    api_ImuSettings settings = imuSettingsDefaults();
    CHECK(validImuSettings(settings));
    settings.mounting[0] = 2;  // a stretch, not a rotation
    CHECK(!validImuSettings(settings));
    settings = imuSettingsDefaults();
    settings.fusion_gain = 0;
    CHECK(!validImuSettings(settings));
    settings = imuSettingsDefaults();
    settings.mag_soft_iron[4] = 0;  // singular: would flatten one axis of the field
    CHECK(!validImuSettings(settings));
}

int main() {
    aStillRobotsGyroBiasIsRemoved();
    aMovingRobotKeepsThePreviousBias();
    theMountingTurnsChipReadingsIntoTheBodyFrame();
    aTiltedRobotIsFoundWithinTheFastStart();
    withoutAMagnetometerYawIsMarkedAsDrifting();
    anAbsentImuReportsNothing();
    aChipsOwnFusionIsTurnedIntoTheBodyFrame();
    settingsWithoutImuUseTheDefaults();
    onlySensibleImuSettingsAreAccepted();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
