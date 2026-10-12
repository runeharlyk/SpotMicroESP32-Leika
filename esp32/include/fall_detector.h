#pragma once

#include <cmath>
#include <cstdint>
#include <peripherals/imu/imu_math.h>

/**
 * Tells a fallen robot from a tilted one: the body has leaned past FALL_TILT_RAD for FALL_HOLD_US. Far beyond the
 * stand's compensation limits (20 degrees roll, 15 pitch) and the steepest slope it walks, and held long enough
 * that a stumble or a bump that rights itself does not count.
 */
class FallDetector {
  public:
    static constexpr float FALL_TILT_RAD = 1.0471976f;  // 60 degrees
    static constexpr int64_t FALL_HOLD_US = 300000;

    /** Whether the robot has fallen, given the latest sample; a sample without an orientation tells nothing. */
    bool update(const ImuSample &imu, int64_t nowUs) {
        if (!(imu.valid & ImuValid::ORIENTATION) || tilt(imu) <= FALL_TILT_RAD) {
            _tiltedSinceUs = 0;
            return false;
        }
        if (!_tiltedSinceUs) _tiltedSinceUs = nowUs;
        return nowUs - _tiltedSinceUs >= FALL_HOLD_US;
    }

    void reset() { _tiltedSinceUs = 0; }

  private:
    int64_t _tiltedSinceUs = 0;

    // The angle between the body's up and the world's: gravity points along the body's -z when level.
    static float tilt(const ImuSample &imu) { return std::acos(std::fmax(-1.0f, std::fmin(1.0f, -imu.gravity[2]))); }
};
