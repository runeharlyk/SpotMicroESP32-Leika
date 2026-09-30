// Host test of peripherals/servo_output.h, built and run by test_host_programs.py.
#include <cmath>
#include <cstdio>
#include <peripherals/servo_output.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static api_Servo servo(int32_t direction = 1, float centerAngle = 0) {
    api_Servo s = api_Servo_init_zero;
    s.center_pwm = 306;
    s.direction = direction;
    s.center_angle = centerAngle;
    s.conversion = 2.0f;
    return s;
}

static api_ServoSettings twelveServos() {
    api_ServoSettings settings = api_ServoSettings_init_zero;
    settings.servos_count = 12;
    for (int i = 0; i < 12; i++) settings.servos[i] = servo();
    return settings;
}

static void anAngleMapsThroughTheCalibration() {
    CHECK(servoPwm(servo(), 0) == 306);
    CHECK(servoPwm(servo(), 10) == 326);
    CHECK(servoPwm(servo(-1, 45), 10) == 376);
}

// Read at run time, as on the robot: with constants the compiler folds the conversion and hides a wrap.
static float runtime(float value) {
    volatile float held = value;
    return held;
}

static void theOutputStaysInsideTheServoRange() {
    CHECK(servoPwm(servo(), runtime(1000)) == SERVO_PWM_MAX);
    CHECK(servoPwm(servo(), runtime(-1000)) == SERVO_PWM_MIN);
    // A slightly negative PWM must not wrap round to the far end stop.
    api_Servo low = servo();
    low.center_pwm = 0;
    CHECK(servoPwm(low, runtime(-3.0f)) == SERVO_PWM_MIN);
}

static void aNonFiniteAngleHoldsTheCentre() {
    CHECK(servoPwm(servo(), NAN) == 306);
    CHECK(servoPwm(servo(), INFINITY) == 306);
}

static void aRawCalibrationPwmIsBounded() {
    CHECK(boundedPwm(0) == SERVO_PWM_MIN);
    CHECK(boundedPwm(4095) == SERVO_PWM_MAX);
    CHECK(boundedPwm(400) == 400);
}

static void settingsTheControllerCannotDriveAreRefused() {
    CHECK(validServoSettings(twelveServos()));

    api_ServoSettings settings = twelveServos();
    settings.servos_count = 11;
    CHECK(!validServoSettings(settings));

    settings = twelveServos();
    settings.servos[3].direction = 0;
    CHECK(!validServoSettings(settings));

    settings = twelveServos();
    settings.servos[3].conversion = NAN;
    CHECK(!validServoSettings(settings));

    settings = twelveServos();
    settings.servos[3].conversion = 0;
    CHECK(!validServoSettings(settings));

    settings = twelveServos();
    settings.servos[3].center_pwm = 700;
    CHECK(!validServoSettings(settings));

    settings = twelveServos();
    settings.servos[3].center_angle = INFINITY;
    CHECK(!validServoSettings(settings));
}

int main() {
    anAngleMapsThroughTheCalibration();
    theOutputStaysInsideTheServoRange();
    aNonFiniteAngleHoldsTheCentre();
    aRawCalibrationPwmIsBounded();
    settingsTheControllerCannotDriveAreRefused();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
