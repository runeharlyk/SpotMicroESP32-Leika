#pragma once

#include <cstdint>
#include <peripherals/imu/imu_math.h>

constexpr float STANDARD_GRAVITY = 9.80665f;
constexpr float RAD_PER_DEG = 0.017453292f;

/** One reading in the chip's own frame and SI units; the magnetometer and orientation only when the chip has them. */
struct RawImu {
    Vec3 accel {};          // m/s^2, specific force
    Vec3 gyro {};           // rad/s
    float temperature = 0;  // degrees Celsius
    bool hasMag = false;
    Vec3 mag {};            // microtesla, already in the accelerometer's axes
    bool hasOrientation = false;
    Quat quat {1, 0, 0, 0}; // the chip's own fusion, chip frame to world
};

class ImuDriver {
  public:
    virtual ~ImuDriver() = default;
    virtual bool begin() = 0;
    /** False when the read failed; `raw` is then unchanged. */
    virtual bool read(RawImu &raw) = 0;
    virtual const char *name() const = 0;
    virtual uint32_t rateHz() const = 0;
    /** The rate of a magnetometer inside the same chip, 0 without one. */
    virtual uint32_t magRateHz() const { return 0; }
};

/** A magnetometer on its own chip. */
class MagDriver {
  public:
    virtual ~MagDriver() = default;
    virtual bool begin() = 0;
    virtual bool read(Vec3 &microtesla) = 0;
    virtual const char *name() const = 0;
    virtual uint32_t rateHz() const = 0;
};
