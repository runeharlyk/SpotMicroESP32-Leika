// Host test of peripherals/imu/imu.h and settings/imu_settings.h with scripted drivers.
#include <cmath>
#include <cstdio>
#include <string>
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
    int begins = 0;
    const char *label = "scripted";
    bool begin() override {
        begins++;
        return present;
    }
    bool read(RawImu &raw) override {
        raw = script[next < script.size() ? next++ : script.size() - 1];
        return true;
    }
    const char *name() const override { return label; }
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
        if (!present) return false;
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

// Calibrating from the app on a tilted robot: the first sample after it must not integrate the second spent
// calibrating as one step.
static void aRecalibrationDoesNotSpinTheOrientation() {
    ScriptedImu driver;
    RawImu raw = still();
    raw.accel = {0, 4.905f, 8.4957f};  // rolled 30 degrees
    driver.script = {raw};
    Imu imu(&driver, nullptr);
    CHECK(imu.begin(0));
    ImuSample sample;
    int64_t clock = 0;
    for (; clock <= 3000000; clock += 5000) imu.update(clock, sample);
    CHECK(near(sample.rpy[0], 0.5236f, 0.02f));
    CHECK(imu.estimateGyroBias([&] { return clock += 5000; }));
    CHECK(imu.update(clock += 5000, sample));
    CHECK(near(sample.rpy[0], 0.5236f, 0.035f));
}

constexpr float DEG = 0.01745329f;
constexpr float G = 9.81f;
constexpr Mat3 TURN = {0, -1, 0, 1, 0, 0, 0, 0, 1};  // the Pico's chip: a quarter turn about z

static Mat3 rotX(float a) { return {1, 0, 0, 0, std::cos(a), -std::sin(a), 0, std::sin(a), std::cos(a)}; }
static Mat3 rotY(float a) { return {std::cos(a), 0, std::sin(a), 0, 1, 0, -std::sin(a), 0, std::cos(a)}; }
static Mat3 transpose(const Mat3 &m) { return {m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]}; }
static bool nearVec(const Vec3 &a, const Vec3 &b, float tolerance) {
    return near(a[0], b[0], tolerance) && near(a[1], b[1], tolerance) && near(a[2], b[2], tolerance);
}

// What a chip mounted as `truth` (chip to body) reads on a level, still robot.
static Vec3 chipAccelAtRest(const Mat3 &truth) { return mul(transpose(truth), Vec3 {0, 0, G}); }

static api_ImuSettings turned() {
    api_ImuSettings settings = imuSettingsDefaults();
    std::copy(TURN.begin(), TURN.end(), settings.mounting);
    return settings;
}

// The Pico's case: the chip sits rolled 5 and pitched 2 degrees under its quarter turn. Levelling folds the tilt
// into the mounting and keeps the turn: the chip's x axis still points where it really does.
static void aTiltedChipIsLevelledAndKeepsItsTurn() {
    const Mat3 truth = mul(TURN, mul(rotX(5 * DEG), rotY(2 * DEG)));
    const Vec3 chip = chipAccelAtRest(truth);
    api_ImuSettings settings = turned();
    float tiltDeg = 0;
    CHECK(levelImuSettings(settings, mul(TURN, chip), tiltDeg));
    CHECK(near(tiltDeg, 5.385f, 0.01f));
    const Mat3 mounting = toMat3(settings.mounting);
    CHECK(isRotation(mounting, 1e-4f));
    CHECK(nearVec(mul(mounting, chip), {0, 0, G}, 1e-3f));
    CHECK(nearVec(mul(mounting, Vec3 {1, 0, 0}), mul(truth, Vec3 {1, 0, 0}), 0.005f));
}

// A tilt this large means the robot is not on a level surface, or the mounting is wrong: nothing is changed.
static void aTiltBeyondTheLimitIsRefused() {
    const Vec3 chip = chipAccelAtRest(mul(TURN, rotX(20 * DEG)));
    api_ImuSettings settings = turned();
    float tiltDeg = 0;
    CHECK(!levelImuSettings(settings, mul(TURN, chip), tiltDeg));
    CHECK(near(tiltDeg, 20, 0.01f));
    CHECK(toMat3(settings.mounting) == TURN);
}

// The bias belongs to the chip: a mounting changed after it was measured must not turn it into a false rate.
static void theGyroBiasFollowsAMountingChange() {
    ScriptedImu driver;
    driver.script = {still({0.01f, -0.02f, 0.005f})};
    Imu imu(&driver, nullptr);
    CHECK(imu.begin(0));
    int64_t clock = 0;
    CHECK(imu.estimateGyroBias([&] { return clock += 5000; }));
    ImuConfig config;
    config.mounting = TURN;
    imu.configure(config);
    ImuSample sample;
    CHECK(imu.update(clock += 5000, sample));
    CHECK(nearVec(sample.gyro, {0, 0, 0}, 1e-5f));
}

// From the button to the reading: the still second's mean accelerometer levels the mounting, and the fused
// orientation of the level robot then reads level.
static void aCalibratedRobotReadsLevel() {
    ScriptedImu driver;
    RawImu raw = still();
    raw.accel = chipAccelAtRest(mul(TURN, mul(rotX(5 * DEG), rotY(2 * DEG))));
    driver.script = {raw};
    Imu imu(&driver, nullptr);
    CHECK(imu.begin(0));
    api_ImuSettings settings = turned();
    imu.configure(imuConfigFrom(settings));
    int64_t clock = 0;
    Vec3 up {};
    CHECK(imu.estimateGyroBias([&] { return clock += 5000; }, &up));
    CHECK(nearVec(up, mul(TURN, raw.accel), 1e-4f));
    float tiltDeg = 0;
    CHECK(levelImuSettings(settings, up, tiltDeg));
    imu.configure(imuConfigFrom(settings));
    ImuSample sample;
    for (int64_t end = clock + 3000000; clock <= end; clock += 5000) imu.update(clock, sample);
    CHECK(near(sample.rpy[0], 0, 0.2f * DEG) && near(sample.rpy[1], 0, 0.2f * DEG));
}

// A compass that stops answering must not hold the heading where it was: the fusion drops to six axes.
static void aCompassThatStopsAnsweringFallsBackToSixAxis() {
    ScriptedImu driver;
    driver.script = {still()};
    ScriptedMag mag;
    Imu imu(&driver, &mag);
    CHECK(imu.begin(0));
    ImuSample sample;
    int64_t clock = 0;
    for (; clock <= 500000; clock += 5000) imu.update(clock, sample);
    CHECK(sample.valid & ImuValid::MAG);
    mag.present = false;  // reads now fail
    for (; clock <= 1000000; clock += 5000) imu.update(clock, sample);
    CHECK(!(sample.valid & ImuValid::MAG));
    CHECK(sample.valid & ImuValid::YAW_DRIFTS);
}

// MPU6050 and ICM-20948 share 0x68: the first chip in the order that answers is the one used, and those after it
// are left alone, since starting one resets whatever sits at that address.
static void theFirstCandidateThatAnswersIsUsed() {
    ScriptedImu absent, first, second;
    absent.present = false;
    first.label = "first";
    second.label = "second";
    for (ScriptedImu *driver : {&absent, &first, &second}) driver->script = {still()};
    Imu imu({&absent, &first, &second}, nullptr);
    CHECK(imu.begin(0));
    CHECK(std::string(imu.driverName()) == "first");
    CHECK(absent.begins == 1 && first.begins == 1 && second.begins == 0);
}

static void aReprobeAfterTheChipLeftReportsNoImu() {
    ScriptedImu driver;
    driver.script = {still()};
    Imu imu({&driver}, nullptr);
    CHECK(imu.begin(0));
    driver.present = false;
    CHECK(!imu.begin(10000));
    CHECK(std::string(imu.driverName()) == "none");
    ImuSample sample;
    CHECK(!imu.update(15000, sample));
}

// A compass wired but not mounted reads the robot's own motors: with the compass off, neither a separate one nor the
// chip's own feeds the fusion.
static void aDisabledCompassIsNeverUsed() {
    ScriptedImu driver;
    RawImu raw = still();
    raw.hasMag = true;
    raw.mag = {20, 0, -40};
    driver.script = {raw};
    ScriptedMag mag;
    Imu imu(&driver, &mag);
    CHECK(imu.begin(0, false));
    CHECK(imu.magRateHz() == 0 && !imu.hasMag());
    ImuSample sample;
    for (int64_t t = 5000; t <= 500000; t += 5000) CHECK(imu.update(t, sample));
    CHECK(mag.reads == 0);
    CHECK(!(sample.valid & ImuValid::MAG));
    CHECK(sample.valid & ImuValid::YAW_DRIFTS);
}

static void aStoppedImuReportsNothing() {
    ScriptedImu driver;
    driver.script = {still()};
    Imu imu(&driver, nullptr);
    CHECK(imu.begin(0));
    imu.stop();
    CHECK(!imu.ready() && imu.rateHz() == 0);
    ImuSample sample;
    CHECK(!imu.update(5000, sample));
}

int main() {
    theFirstCandidateThatAnswersIsUsed();
    aReprobeAfterTheChipLeftReportsNoImu();
    aDisabledCompassIsNeverUsed();
    aStoppedImuReportsNothing();
    aStillRobotsGyroBiasIsRemoved();
    aMovingRobotKeepsThePreviousBias();
    theMountingTurnsChipReadingsIntoTheBodyFrame();
    aTiltedRobotIsFoundWithinTheFastStart();
    withoutAMagnetometerYawIsMarkedAsDrifting();
    anAbsentImuReportsNothing();
    aChipsOwnFusionIsTurnedIntoTheBodyFrame();
    settingsWithoutImuUseTheDefaults();
    onlySensibleImuSettingsAreAccepted();
    aRecalibrationDoesNotSpinTheOrientation();
    aCompassThatStopsAnsweringFallsBackToSixAxis();
    aTiltedChipIsLevelledAndKeepsItsTurn();
    aTiltBeyondTheLimitIsRefused();
    theGyroBiasFollowsAMountingChange();
    aCalibratedRobotReadsLevel();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
