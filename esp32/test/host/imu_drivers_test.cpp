// Host test of the IMU chip drivers against the register-serving I2C fake (stubs/driver/i2c_master.h).
#include <cmath>
#include <cstdio>
#include <peripherals/drivers/mpu6050.h>
#include <peripherals/drivers/hmc5883l.h>
#include <peripherals/drivers/bno055.h>
#include <peripherals/drivers/icm20948.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static bool near(float a, float b, float tolerance) { return std::fabs(a - b) < tolerance; }

static void putBigEndian(uint8_t address, uint8_t reg, int16_t value) {
    fake_i2c::registers[address][reg] = static_cast<uint8_t>(value >> 8);
    fake_i2c::registers[address][reg + 1] = static_cast<uint8_t>(value & 0xFF);
}

static void startBus() {
    fake_i2c::reset();
    I2CBus::instance().begin(21, 22);
}

static void mpu6050ReportsSiUnits() {
    startBus();
    fake_i2c::registers[0x68][0x75] = 0x68;  // WHO_AM_I
    MPU6050Driver mpu;
    CHECK(mpu.begin());
    CHECK(fake_i2c::registers[0x68][0x1B] == 0x08);  // +-500 deg/s
    CHECK(fake_i2c::registers[0x68][0x1C] == 0x08);  // +-4 g
    CHECK(fake_i2c::registers[0x68][0x19] == 4);     // 1 kHz / 5 = 200 Hz
    CHECK(fake_i2c::registers[0x68][0x37] == 0x02);  // bypass, so a compass behind it is reachable
    putBigEndian(0x68, 0x3B, 8192);    // +1 g on x
    putBigEndian(0x68, 0x3D, -4096);   // -0.5 g on y
    putBigEndian(0x68, 0x3F, 0);
    putBigEndian(0x68, 0x41, 340);     // 1 degree above 36.53
    putBigEndian(0x68, 0x43, 655);     // 10 deg/s on x
    putBigEndian(0x68, 0x45, 0);
    putBigEndian(0x68, 0x47, -6550);   // -100 deg/s on z
    RawImu raw;
    CHECK(mpu.read(raw));
    CHECK(near(raw.accel[0], 9.80665f, 1e-3f));
    CHECK(near(raw.accel[1], -4.903325f, 1e-3f));
    CHECK(near(raw.gyro[0], 10 * 0.017453292f, 1e-4f));
    CHECK(near(raw.gyro[2], -100 * 0.017453292f, 1e-3f));
    CHECK(near(raw.temperature, 37.53f, 1e-3f));
    CHECK(!raw.hasMag && !raw.hasOrientation);
    I2CBus::instance().end();
}

static void mpu6050RefusesAnotherChip() {
    startBus();
    fake_i2c::registers[0x68][0x75] = 0xEA;  // an ICM-20948 at the same address
    MPU6050Driver mpu;
    CHECK(!mpu.begin());
    I2CBus::instance().end();
}

static void putLittleEndian(uint8_t address, uint8_t reg, int16_t value) {
    fake_i2c::registers[address][reg] = static_cast<uint8_t>(value & 0xFF);
    fake_i2c::registers[address][reg + 1] = static_cast<uint8_t>(value >> 8);
}

static void hmc5883ReportsMicrotesla() {
    startBus();
    fake_i2c::registers[0x1E][0x0A] = 'H';
    fake_i2c::registers[0x1E][0x0B] = '4';
    fake_i2c::registers[0x1E][0x0C] = '3';
    HMC5883LDriver hmc;
    CHECK(hmc.begin());
    CHECK(fake_i2c::registers[0x1E][0x00] == 0x78);  // 8-sample average, 75 Hz
    CHECK(fake_i2c::registers[0x1E][0x01] == 0x20);  // 1090 LSB per gauss
    putBigEndian(0x1E, 0x03, 1090);   // x: 1 gauss = 100 microtesla
    putBigEndian(0x1E, 0x05, -545);   // z comes before y on this chip
    putBigEndian(0x1E, 0x07, 218);    // y
    Vec3 mag;
    CHECK(hmc.read(mag));
    CHECK(near(mag[0], 100.0f, 1e-3f));
    CHECK(near(mag[1], 20.0f, 1e-3f));
    CHECK(near(mag[2], -50.0f, 1e-3f));
    I2CBus::instance().end();
}

// -4096 is the chip's overflow marker: the reading is not a field value.
static void hmc5883RefusesAnOverflow() {
    startBus();
    fake_i2c::registers[0x1E][0x0A] = 'H';
    fake_i2c::registers[0x1E][0x0B] = '4';
    fake_i2c::registers[0x1E][0x0C] = '3';
    HMC5883LDriver hmc;
    CHECK(hmc.begin());
    putBigEndian(0x1E, 0x03, -4096);
    Vec3 mag {1, 2, 3};
    CHECK(!hmc.read(mag));
    CHECK(mag[0] == 1);
    I2CBus::instance().end();
}

static void bno055ReportsItsOwnFusionAndRawReadings() {
    startBus();
    fake_i2c::registers[0x29][0x00] = 0xA0;  // chip id, also read back after the reset
    BNO055Driver bno;
    CHECK(bno.begin());
    putLittleEndian(0x29, 0x08, 981);    // accel x: 100 LSB per m/s^2
    putLittleEndian(0x29, 0x0E, 320);    // mag x: 16 LSB per microtesla
    putLittleEndian(0x29, 0x14, -160);   // gyro x: 16 LSB per deg/s
    putLittleEndian(0x29, 0x20, 16384);  // quaternion w: 2^14 LSB per unit
    fake_i2c::registers[0x29][0x34] = 31;
    RawImu raw;
    CHECK(bno.read(raw));
    CHECK(near(raw.accel[0], 9.81f, 1e-3f));
    CHECK(near(raw.mag[0], 20.0f, 1e-3f));
    CHECK(near(raw.gyro[0], -10 * 0.017453292f, 1e-4f));
    CHECK(raw.hasMag && raw.hasOrientation);
    CHECK(near(raw.quat[0], 1.0f, 1e-4f));
    CHECK(near(raw.temperature, 31.0f, 1e-4f));
    I2CBus::instance().end();
}

static void icm20948ReportsSiUnitsAndItsCompass() {
    startBus();
    fake_i2c::registers[0x68][0x00] = 0xEA;  // WHO_AM_I in bank 0
    fake_i2c::registers[0x0C][0x01] = 0x09;  // AK09916 WIA2
    ICM20948Driver icm;
    CHECK(icm.begin());
    CHECK(fake_i2c::registers[0x68][0x0F] == 0x02);  // bypass
    CHECK(fake_i2c::registers[0x0C][0x31] == 0x08);  // compass continuous 100 Hz
    CHECK(fake_i2c::registers[0x68][0x7F] == 0x00);  // left in bank 0, where the data lives
    // Bank 2 writes land at the same addresses in this flat fake: re-seed bank 0 data after begin().
    putBigEndian(0x68, 0x2D, 8192);   // accel x +1 g
    putBigEndian(0x68, 0x2F, 0);
    putBigEndian(0x68, 0x31, -8192);  // accel z -1 g
    putBigEndian(0x68, 0x33, 655);    // gyro x 10 deg/s
    putBigEndian(0x68, 0x35, 0);
    putBigEndian(0x68, 0x37, 0);
    putBigEndian(0x68, 0x39, 3339);   // about 31 degC
    fake_i2c::registers[0x0C][0x10] = 0x01;  // data ready
    putLittleEndian(0x0C, 0x11, 100);  // 15 microtesla on the compass's x
    putLittleEndian(0x0C, 0x13, 200);  // 30 on its y
    putLittleEndian(0x0C, 0x15, -300); // -45 on its z
    fake_i2c::registers[0x0C][0x18] = 0x00;
    RawImu raw;
    CHECK(icm.read(raw));
    CHECK(near(raw.accel[0], 9.80665f, 1e-3f));
    CHECK(near(raw.accel[2], -9.80665f, 1e-3f));
    CHECK(near(raw.gyro[0], 10 * 0.017453292f, 1e-4f));
    CHECK(near(raw.temperature, 31.0f, 0.05f));
    CHECK(raw.hasMag && !raw.hasOrientation);
    CHECK(near(raw.mag[0], 15.0f, 1e-3f));
    CHECK(near(raw.mag[1], -30.0f, 1e-3f));  // compass y and z point against the accelerometer's
    CHECK(near(raw.mag[2], 45.0f, 1e-3f));
    I2CBus::instance().end();
}

// No new compass data, or an overflowed one: the accelerometer and gyro still come through.
static void icm20948SkipsACompassReadingThatIsNotThere() {
    startBus();
    fake_i2c::registers[0x68][0x00] = 0xEA;
    fake_i2c::registers[0x0C][0x01] = 0x09;
    ICM20948Driver icm;
    CHECK(icm.begin());
    fake_i2c::registers[0x0C][0x10] = 0x00;
    RawImu raw;
    CHECK(icm.read(raw));
    CHECK(!raw.hasMag);
    fake_i2c::registers[0x0C][0x10] = 0x01;
    fake_i2c::registers[0x0C][0x18] = 0x08;  // HOFL
    CHECK(icm.read(raw));
    CHECK(!raw.hasMag);
    I2CBus::instance().end();
}

int main() {
    mpu6050ReportsSiUnits();
    mpu6050RefusesAnotherChip();
    hmc5883ReportsMicrotesla();
    hmc5883RefusesAnOverflow();
    bno055ReportsItsOwnFusionAndRawReadings();
    icm20948ReportsSiUnitsAndItsCompass();
    icm20948SkipsACompassReadingThatIsNotThere();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
