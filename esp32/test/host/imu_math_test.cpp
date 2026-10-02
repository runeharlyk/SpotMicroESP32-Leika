// Host test of peripherals/imu/imu_math.h, built and run by test_host_programs.py.
#include <cmath>
#include <cstdio>
#include <peripherals/imu/imu_math.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) < tolerance; }
static bool near(const Vec3 &a, const Vec3 &b, float tolerance = 1e-4f) {
    return near(a[0], b[0], tolerance) && near(a[1], b[1], tolerance) && near(a[2], b[2], tolerance);
}

// Rotation about one body axis by `angle`, as a quaternion rotating body vectors into the world.
static Quat aboutAxis(int axis, float angle) {
    Quat q {std::cos(angle / 2), 0, 0, 0};
    q[1 + axis] = std::sin(angle / 2);
    return q;
}

static constexpr float DEG = 0.017453292f;

static void levelMeansGravityStraightDown() {
    CHECK(near(gravityInBody({1, 0, 0, 0}), Vec3 {0, 0, -1}));
    CHECK(near(rpyFromQuat({1, 0, 0, 0}), Vec3 {0, 0, 0}));
}

// REP-103: positive roll lifts the left side, positive pitch lowers the nose, positive yaw turns left.
static void tiltsHaveTheSimsSigns() {
    CHECK(near(gravityInBody(aboutAxis(0, 30 * DEG)), Vec3 {0, -0.5f, -0.8660254f}));
    CHECK(near(rpyFromQuat(aboutAxis(0, 30 * DEG)), Vec3 {30 * DEG, 0, 0}));
    CHECK(near(gravityInBody(aboutAxis(1, 30 * DEG)), Vec3 {0.5f, 0, -0.8660254f}));
    CHECK(near(rpyFromQuat(aboutAxis(1, 30 * DEG)), Vec3 {0, 30 * DEG, 0}));
    CHECK(near(rpyFromQuat(aboutAxis(2, 30 * DEG)), Vec3 {0, 0, 30 * DEG}));
}

static void aMountingRotatesEveryAxis() {
    const Mat3 turnedAround = {-1, 0, 0, 0, -1, 0, 0, 0, 1};
    CHECK(near(mul(turnedAround, Vec3 {1, 2, 3}), Vec3 {-1, -2, 3}));
    const Mat3 rolledOver = {1, 0, 0, 0, -1, 0, 0, 0, -1};
    CHECK(near(mul(mul(rolledOver, turnedAround), Vec3 {1, 2, 3}), Vec3 {-1, 2, -3}));
}

static void aMatrixAndItsQuaternionRotateAlike() {
    const Mat3 yaw90 = {0, -1, 0, 1, 0, 0, 0, 0, 1};
    const Quat q = quatFromMatrix(yaw90);
    CHECK(near(rpyFromQuat(q), Vec3 {0, 0, 90 * DEG}));
    const Mat3 turnedAround = {-1, 0, 0, 0, -1, 0, 0, 0, 1};
    CHECK(near(std::fabs(quatFromMatrix(turnedAround)[3]), 1.0f));
    // Undoing a mounting: a chip turned 90 degrees on a level robot reports a 90 degree yaw; the body is level.
    const Quat body = quatMul(aboutAxis(2, 90 * DEG), quatConj(quatFromMatrix(yaw90)));
    CHECK(near(rpyFromQuat(body), Vec3 {0, 0, 0}));
}

static void onlyRotationsAreRotations() {
    CHECK(isRotation(IDENTITY3, 1e-3f));
    CHECK(isRotation({0, -1, 0, 1, 0, 0, 0, 0, 1}, 1e-3f));
    CHECK(!isRotation({0, 0, 0, 0, 0, 0, 0, 0, 0}, 1e-3f));
    CHECK(!isRotation({-1, 0, 0, 0, 1, 0, 0, 0, 1}, 1e-3f));  // a mirror
    CHECK(!isRotation({2, 0, 0, 0, 1, 0, 0, 0, 1}, 1e-3f));   // a stretch
}

int main() {
    levelMeansGravityStraightDown();
    tiltsHaveTheSimsSigns();
    aMountingRotatesEveryAxis();
    aMatrixAndItsQuaternionRotateAlike();
    onlyRotationsAreRotations();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
