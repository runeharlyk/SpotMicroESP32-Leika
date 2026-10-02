// Host test of peripherals/imu/madgwick.h, built and run by test_host_programs.py.
#include <cmath>
#include <cstdio>
#include <peripherals/imu/madgwick.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static constexpr float DEG = 0.017453292f;
static constexpr float DT = 0.005f; // 200 Hz
static const Vec3 UP_FORCE = {0, 0, 9.81f};
static const Vec3 EARTH_FIELD = {20, 0, -40}; // microtesla: north along world x, dipping down

static Quat fromRpy(float roll, float pitch, float yaw) {
    const float cr = std::cos(roll / 2), sr = std::sin(roll / 2), cp = std::cos(pitch / 2), sp = std::sin(pitch / 2),
                cy = std::cos(yaw / 2), sy = std::sin(yaw / 2);
    return {cr * cp * cy + sr * sp * sy, sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy};
}

// A world vector as the body sees it: R(q)^T v.
static Vec3 inBody(const Quat &q, const Vec3 &v) {
    const Quat r = quatMul(quatMul(quatConj(q), {0, v[0], v[1], v[2]}), q);
    return {r[1], r[2], r[3]};
}

static float angleBetween(const Quat &a, const Quat &b) {
    const float dot = std::fabs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
    return 2 * std::acos(std::fmin(dot, 1.0f));
}

static void itFindsAStillOrientationFromAccelerometerAndCompass() {
    const Quat truth = fromRpy(20 * DEG, -10 * DEG, 30 * DEG);
    Madgwick filter;
    filter.setGain(0.5f);
    for (int step = 0; step < 2000; step++)
        filter.update({0, 0, 0}, inBody(truth, UP_FORCE), inBody(truth, EARTH_FIELD), DT);
    CHECK(angleBetween(filter.quaternion(), truth) < 1 * DEG);
}

static void withoutACompassItStillFindsTheTilt() {
    const Quat truth = fromRpy(-15 * DEG, 25 * DEG, 0);
    Madgwick filter;
    filter.setGain(0.5f);
    for (int step = 0; step < 2000; step++) filter.updateImu({0, 0, 0}, inBody(truth, UP_FORCE), DT);
    const Vec3 rpy = rpyFromQuat(filter.quaternion());
    CHECK(std::fabs(rpy[0] - -15 * DEG) < 1 * DEG);
    CHECK(std::fabs(rpy[1] - 25 * DEG) < 1 * DEG);
}

// Turning on the spot at 1 rad/s for 1 s: the gyro carries the turn, the compass agrees with it.
static void itFollowsATurnTheGyroMeasures() {
    Madgwick filter;
    filter.setGain(0.1f);
    for (int step = 0; step < 200; step++) {
        const Quat truth = fromRpy(0, 0, (step + 1) * DT);
        filter.update({0, 0, 1}, inBody(truth, UP_FORCE), inBody(truth, EARTH_FIELD), DT);
    }
    CHECK(std::fabs(rpyFromQuat(filter.quaternion())[2] - 1.0f) < 0.02f);
}

static void aTurnWithoutCompassIsIntegrated() {
    Madgwick filter;
    filter.setGain(0.1f);
    for (int step = 0; step < 200; step++) filter.updateImu({0, 0, 1}, UP_FORCE, DT);
    CHECK(std::fabs(rpyFromQuat(filter.quaternion())[2] - 1.0f) < 0.02f);
}

// A zero accelerometer (free fall, or a read of all zeros) must not poison the quaternion.
static void aZeroAccelerometerIsSkipped() {
    Madgwick filter;
    filter.setGain(0.5f);
    for (int step = 0; step < 100; step++) filter.update({0, 0, 0}, {0, 0, 0}, {0, 0, 0}, DT);
    const Quat q = filter.quaternion();
    CHECK(std::fabs(q[0] - 1) < 1e-6f && std::fabs(q[1]) < 1e-6f && std::fabs(q[2]) < 1e-6f &&
          std::fabs(q[3]) < 1e-6f);
}

int main() {
    itFindsAStillOrientationFromAccelerometerAndCompass();
    withoutACompassItStillFindsTheTilt();
    itFollowsATurnTheGyroMeasures();
    aTurnWithoutCompassIsIntegrated();
    aZeroAccelerometerIsSkipped();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
