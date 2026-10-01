// Host test of the IMU chip drivers against the register-serving I2C fake (stubs/driver/i2c_master.h).
#include <cmath>
#include <cstdio>
#include <peripherals/drivers/mpu6050.h>

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

int main() {
    mpu6050ReportsSiUnits();
    mpu6050RefusesAnotherChip();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
