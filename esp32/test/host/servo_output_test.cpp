// Host test of peripherals/servo_output.h, built and run by test_host_programs.py.
#include <cmath>
#include <cstdio>
#include <joint_model.h>
#include <peripherals/servo_output.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// The PWM today's firmware wrote from per-servo settings, kept here as the reference the tables must reproduce.
static float oldPwm(float direction, float centerAngle, float conversion, float centerPwm, float angle) {
    return (direction * angle + centerAngle) * conversion + centerPwm;
}

// What the robots carry today: the factory table on the full-size robot, the user's calibration on the Pico.
static const float OLD_FULL_DIRECTION[12] = {-1, 1, 1, -1, -1, -1, 1, 1, 1, 1, -1, -1};
static const float OLD_FULL_CENTER[12] = {0, -45, 90, 0, 45, -90, 0, -45, 90, 0, 45, -90};
static const float OLD_PICO_DIRECTION[12] = {1, 1, -1, 1, -1, 1, -1, 1, -1, -1, -1, 1};
static const float OLD_PICO_CENTER[12] = {0, -45, -90, 0, 45, 90, 0, -45, -90, 0, 45, 90};
static const float PICO_CENTER_PWM[12] = {295, 243, 291, 268, 305, 270, 258, 260, 287, 289, 301, 273};

static void eachVariantTableReproducesTheSettingsItReplaces() {
    for (size_t joint = 0; joint < 12; joint++) {
        for (float angle : {-40.0f, -5.0f, 0.0f, 12.5f, 35.0f}) {
            const float full = oldPwm(OLD_FULL_DIRECTION[joint], OLD_FULL_CENTER[joint], 2, 306, angle);
            CHECK(servoPwm(JOINT_MODEL_SPOTMICRO_ESP32, joint, 306, angle) == boundedPwm(full));
            CHECK(servoPwm(JOINT_MODEL_SPOTMICRO_YERTLE, joint, 306, angle) == boundedPwm(full));
            const float pico =
                oldPwm(OLD_PICO_DIRECTION[joint], OLD_PICO_CENTER[joint], 2, PICO_CENTER_PWM[joint], angle);
            CHECK(servoPwm(JOINT_MODEL_SPOTMICRO_ESP32_MINI, joint, PICO_CENTER_PWM[joint], angle) == boundedPwm(pico));
        }
    }
}

// Read at run time, as on the robot: with constants the compiler folds the conversion and hides a wrap.
static float runtime(float value) {
    volatile float held = value;
    return held;
}

static void theOutputStaysInsideTheServoRange() {
    CHECK(servoPwm(JOINT_MODEL_SPOTMICRO_ESP32, 1, 306, runtime(1000)) == SERVO_PWM_MAX);
    CHECK(servoPwm(JOINT_MODEL_SPOTMICRO_ESP32, 1, 306, runtime(-1000)) == SERVO_PWM_MIN);
    // A slightly negative PWM must not wrap round to the far end stop.
    CHECK(servoPwm(JOINT_MODEL_SPOTMICRO_ESP32, 0, 0, runtime(-3.0f)) == SERVO_PWM_MIN);
}

static void aNonFiniteAngleHoldsTheCentre() {
    // Joint 0 has centre angle 0 in every table: a NaN angle gives the centre PWM itself.
    CHECK(servoPwm(JOINT_MODEL_SPOTMICRO_ESP32, 0, 306, NAN) == 306);
    CHECK(servoPwm(JOINT_MODEL_SPOTMICRO_ESP32, 0, 306, INFINITY) == 306);
}

static void aRawCalibrationPwmIsBounded() {
    CHECK(boundedPwm(0) == SERVO_PWM_MIN);
    CHECK(boundedPwm(4095) == SERVO_PWM_MAX);
    CHECK(boundedPwm(400) == 400);
}

// 700 deg/s, just under the servos' speed, sampled at the control loop's 100 Hz: the limit must not touch it.
static void aMotionTheServosCanFollowPassesUntouched() {
    float angle = 0;
    for (int tick = 1; tick <= 20; tick++) {
        const float target = 7.0f * tick;
        angle = slewToward(angle, target, SERVO_MAX_SPEED_DEG_S * 0.01f);
        CHECK(angle == target);
    }
}

// A jump across the whole range takes as long as the servo itself would: 180 degrees in 250 ms.
static void aJumpTakesAsLongAsTheServoWould() {
    float angle = 0;
    int ticks = 0;
    while (std::fabs(angle - 180) > 1e-3f && ticks < 100) {
        angle = slewToward(angle, 180, SERVO_MAX_SPEED_DEG_S * 0.01f);
        ticks++;
    }
    CHECK(ticks == 25);
    CHECK(slewToward(180, 0, SERVO_MAX_SPEED_DEG_S * 0.01f) < 180);
}

// The MG92B's ratings at its two supplies, linear between them, and the controller's 720 deg/s at 6 V exactly, so a
// robot that never set its supply moves as before.
static void theSpeedLimitFollowsTheSupply() {
    CHECK(servoSpeedLimit(DEFAULT_SERVO_SUPPLY_VOLTS) == SERVO_MAX_SPEED_DEG_S);
    CHECK(std::fabs(mg92bRatedSpeed(4.8f) - 461.54f) < 0.01f);
    CHECK(std::fabs(mg92bRatedSpeed(6.0f) - 750) < 0.01f);
    CHECK(std::fabs(mg92bRatedSpeed(5.4f) - (461.54f + 750) / 2) < 0.01f);
    CHECK(std::fabs(servoSpeedLimit(4.8f) - 461.54f * 720 / 750) < 0.01f);
    CHECK(std::fabs(servoSpeedLimit(5.0f) - (461.54f + (750 - 461.54f) / 6) * 720 / 750) < 0.01f);
}

// Outside its ratings the speed is held at the nearer one: a 7.4 V rail is not taken to be faster than 6 V.
static void theSpeedLimitHoldsOutsideTheRatings() {
    CHECK(servoSpeedLimit(7.4f) == SERVO_MAX_SPEED_DEG_S);
    CHECK(servoSpeedLimit(3.0f) == servoSpeedLimit(4.8f));
    CHECK(servoSpeedLimit(0.0f) == servoSpeedLimit(4.8f));
}

int main() {
    eachVariantTableReproducesTheSettingsItReplaces();
    theOutputStaysInsideTheServoRange();
    aNonFiniteAngleHoldsTheCentre();
    aRawCalibrationPwmIsBounded();
    aMotionTheServosCanFollowPassesUntouched();
    aJumpTakesAsLongAsTheServoWould();
    theSpeedLimitFollowsTheSupply();
    theSpeedLimitHoldsOutsideTheRatings();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
