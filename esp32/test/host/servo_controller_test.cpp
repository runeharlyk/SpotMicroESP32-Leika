// Host test of ServoController's channel routing against the register-serving I2C fake.
#include <cmath>
#include <cstdio>
#include <peripherals/servo_controller.h>

namespace FileSystem {
bool mkdirRecursive(const char *) { return true; }
} // namespace FileSystem

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// The PCA9685 at 0x40 keeps each channel's off count in registers 0x08 + 4 * channel (low) and 0x09 + 4 * channel.
static uint16_t offCount(uint32_t channel) {
    auto &chip = fake_i2c::registers[0x40];
    return chip[0x08 + 4 * channel] | (chip[0x09 + 4 * channel] << 8);
}

// Bit 4 of a channel's off-count high register holds it fully off.
static bool fullyOff(uint32_t channel) { return fake_i2c::registers[0x40][0x09 + 4 * channel] & 0x10; }

static const uint32_t REVERSED[12] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4};

static void wireReversed(ServoController &controller) {
    controller.useJointModel(&JOINT_MODEL_SPOTMICRO_ESP32);
    controller.updateWithoutPropagation([](ServoSettings &settings) {
        settings = ServoSettings_defaults();
        settings.channels_count = 12;
        for (int i = 0; i < 12; i++) settings.channels[i] = REVERSED[i];
        return StateUpdateResult::CHANGED;
    });
}

static void allServosMeansEveryMappedChannel() {
    fake_i2c::reset();
    I2CBus::instance().begin(21, 22);
    static ServoController controller;
    wireReversed(controller);
    controller.setServoPWM(-1, 300);
    for (uint32_t channel = 4; channel < 16; channel++) CHECK(offCount(channel) == 300);
    for (uint32_t channel = 0; channel < 4; channel++) CHECK(offCount(channel) == 0);
    controller.setServoPWM(2, 250);
    CHECK(offCount(2) == 250);
    I2CBus::instance().end();
}

static void eachJointsAngleReachesItsChannel() {
    fake_i2c::reset();
    I2CBus::instance().begin(21, 22);
    static ServoController controller;
    wireReversed(controller);
    CHECK(controller.calculatePWM(0.01f));
    for (size_t joint = 0; joint < 12; joint++) CHECK(offCount(REVERSED[joint]) == controller.outputPwm()[joint]);
    for (uint32_t channel = 0; channel < 4; channel++) CHECK(fullyOff(channel));
    I2CBus::instance().end();
}

// The joints move toward their targets no faster than the servos can, and a stalled tick (a long dt) does not
// license a jump.
static void theJointsMoveNoFasterThanTheServos() {
    fake_i2c::reset();
    I2CBus::instance().begin(21, 22);
    static ServoController controller;
    wireReversed(controller);
    float start[12], targets[12];
    std::copy(controller.outputAngles(), controller.outputAngles() + 12, start);
    for (int i = 0; i < 12; i++) targets[i] = start[i] + (i % 2 ? 100.0f : -100.0f);
    controller.setAngles(targets);
    CHECK(controller.calculatePWM(0.01f));
    for (int i = 0; i < 12; i++) CHECK(std::fabs(std::fabs(controller.outputAngles()[i] - start[i]) - 7.2f) < 1e-3f);
    std::copy(controller.outputAngles(), controller.outputAngles() + 12, start);
    CHECK(controller.calculatePWM(1.0f));
    for (int i = 0; i < 12; i++)
        CHECK(std::fabs(controller.outputAngles()[i] - start[i]) <= SERVO_MAX_SPEED_DEG_S * ServoController::MAX_TICK_S + 1e-3f);
    I2CBus::instance().end();
}

// Bit 4 of MODE1 holds the chip asleep, every output off.
static bool asleep() { return fake_i2c::registers[0x40][0x00] & 0x10; }

// A Pico driven with the Leika's joint model would push its mirrored knees against their stops.
static void withoutAVariantTheServosStayAsleep() {
    fake_i2c::reset();
    I2CBus::instance().begin(21, 22);
    fake_i2c::registers[0x40][0x00] = 0x10;
    static ServoController controller;
    controller.activate();
    CHECK(asleep());
    controller.setMode(SERVO_CONTROL_STATE::ANGLE);
    const ServoWrite write = controller.update(0.01f);
    CHECK(!write.attempted);
    for (uint32_t channel = 0; channel < 16; channel++) CHECK(offCount(channel) == 0);
    ServoSettings reply = api_ServoSettings_init_zero;
    controller.protoHandler.read(reply);
    CHECK(!reply.has_model);
    I2CBus::instance().end();
}

static void withAVariantActivatingWakesTheServos() {
    fake_i2c::reset();
    I2CBus::instance().begin(21, 22);
    fake_i2c::registers[0x40][0x00] = 0x10;
    static ServoController controller;
    controller.useJointModel(&JOINT_MODEL_SPOTMICRO_ESP32_MINI);
    controller.activate();
    CHECK(!asleep());
    CHECK(controller.update(0.01f).attempted);
    ServoSettings reply = api_ServoSettings_init_zero;
    controller.protoHandler.read(reply);
    CHECK(reply.has_model && reply.model.center_angle[2] == JOINT_MODEL_SPOTMICRO_ESP32_MINI.center_angle[2]);
    I2CBus::instance().end();
}

// A switched variant drives the servos through its own joint model and reports that one.
static void aSwitchedJointModelIsUsedAndReported() {
    fake_i2c::reset();
    I2CBus::instance().begin(21, 22);
    static ServoController controller;
    wireReversed(controller);  // also the Leika's joint model
    CHECK(controller.calculatePWM(0.01f));
    const uint16_t leikaKnee = controller.outputPwm()[2];
    controller.useJointModel(&JOINT_MODEL_SPOTMICRO_ESP32_MINI);
    CHECK(controller.calculatePWM(0.01f));
    CHECK(controller.outputPwm()[2] != leikaKnee);
    ServoSettings reply = api_ServoSettings_init_zero;
    controller.protoHandler.read(reply);
    CHECK(reply.model.direction[0] == JOINT_MODEL_SPOTMICRO_ESP32_MINI.direction[0]);
    I2CBus::instance().end();
}

int main() {
    aSwitchedJointModelIsUsedAndReported();
    withoutAVariantTheServosStayAsleep();
    withAVariantActivatingWakesTheServos();
    allServosMeansEveryMappedChannel();
    eachJointsAngleReachesItsChannel();
    theJointsMoveNoFasterThanTheServos();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
