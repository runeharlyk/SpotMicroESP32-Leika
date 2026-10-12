// Host test of ServoController's channel routing against the register-serving I2C fake.
#include <cmath>
#include <cstdio>
#include <peripherals/servo_controller.h>

namespace FileSystem {
bool mkdirRecursive(const char *) { return true; }
bool replaceFile(const char *, const char *) { return false; }
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
    // A stance inside every joint's travel, at least 20 degrees from where the joints start.
    const float stance[3] = {20, -40, 60};
    for (int i = 0; i < 12; i++) targets[i] = stance[i % 3] * JOINT_DIRECTION[i];
    controller.setAngles(targets);
    CHECK(controller.calculatePWM(0.01f));
    for (int i = 0; i < 12; i++) CHECK(std::fabs(std::fabs(controller.outputAngles()[i] - start[i]) - 7.2f) < 1e-3f);
    std::copy(controller.outputAngles(), controller.outputAngles() + 12, start);
    CHECK(controller.calculatePWM(1.0f));
    for (int i = 0; i < 12; i++)
        CHECK(std::fabs(controller.outputAngles()[i] - start[i]) <= SERVO_MAX_SPEED_DEG_S * ServoController::MAX_TICK_S + 1e-3f);
    I2CBus::instance().end();
}

// A supply saved through the settings slows the joints from the next tick, without a restart, and the walk reads the
// same limit.
static void aSavedSupplySlowsTheJointsAtOnce() {
    fake_i2c::reset();
    I2CBus::instance().begin(21, 22);
    static ServoController controller;
    wireReversed(controller);
    CHECK(controller.speedLimit() == SERVO_MAX_SPEED_DEG_S);
    ServoSettings save = ServoSettings_defaults();
    save.supply_voltage = 4.8f;
    CHECK(controller.protoHandler.update(save) == StateUpdateResult::CHANGED);
    CHECK(controller.speedLimit() == servoSpeedLimit(4.8f));
    float start[12], targets[12];
    std::copy(controller.outputAngles(), controller.outputAngles() + 12, start);
    const float stance[3] = {20, -40, 60};
    for (int i = 0; i < 12; i++) targets[i] = stance[i % 3] * JOINT_DIRECTION[i];
    controller.setAngles(targets);
    CHECK(controller.calculatePWM(0.01f));
    const float step = servoSpeedLimit(4.8f) * 0.01f;
    CHECK(step < 4.5f);
    for (int i = 0; i < 12; i++) CHECK(std::fabs(std::fabs(controller.outputAngles()[i] - start[i]) - step) < 1e-3f);
    ServoSettings reply = api_ServoSettings_init_zero;
    controller.protoHandler.read(reply);
    CHECK(reply.supply_voltage == 4.8f);
    I2CBus::instance().end();
}

// Where the joints settle after long enough to reach their targets, in the IK convention.
static void settle(ServoController &controller, const float ik[3], float settled[12]) {
    float targets[12];
    for (int i = 0; i < 12; i++) targets[i] = ik[i % 3] * JOINT_DIRECTION[i];
    controller.setAngles(targets);
    for (int tick = 0; tick < 100; tick++) controller.calculatePWM(0.02f);
    for (int i = 0; i < 12; i++) settled[i] = controller.outputAngles()[i] * JOINT_DIRECTION[i];
}

// Whatever asks for it, a joint goes no further than its variant's travel, on every leg alike, and a target inside
// the travel passes as it is.
static void aTargetPastTheTravelIsHeldAtItsEnd() {
    fake_i2c::reset();
    I2CBus::instance().begin(21, 22);
    static ServoController controller;
    wireReversed(controller);
    float settled[12];
    const float beyond[3] = {-120, 80, 170};
    settle(controller, beyond, settled);
    const JointRange *travel = JOINT_MODEL_SPOTMICRO_ESP32.travel;
    for (int i = 0; i < 12; i += 3) {
        CHECK(std::fabs(settled[i] - travel[0].min) < 1e-3f);
        CHECK(std::fabs(settled[i + 1] - travel[1].max) < 1e-3f);
        CHECK(std::fabs(settled[i + 2] - travel[2].max) < 1e-3f);
    }
    const float inside[3] = {-30, -60, 120};
    settle(controller, inside, settled);
    for (int i = 0; i < 12; i++) CHECK(std::fabs(settled[i] - inside[i % 3]) < 1e-3f);
    // The Pico's knee folds further than the full-size robot's.
    controller.useJointModel(&JOINT_MODEL_SPOTMICRO_ESP32_MINI);
    settle(controller, beyond, settled);
    for (int i = 2; i < 12; i += 3) CHECK(std::fabs(settled[i] - JOINT_MODEL_SPOTMICRO_ESP32_MINI.travel[2].max) < 1e-3f);
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

// One 10 ms control tick: the controller's write, as the control task makes it.
static ServoWrite tick(ServoController &controller) {
    fake_timer::nowUs += 10000;
    return controller.update(0.01f);
}

// A dropped board fails three writes, then is tried twice a second; every tick in between reports its write not ok.
static void aBoardThatStopsAnsweringIsTriedTwiceASecond() {
    fake_i2c::reset();
    I2CBus::instance().begin(21, 22);
    static ServoController controller;
    wireReversed(controller);
    controller.activate();
    CHECK(tick(controller).ok);
    fake_i2c::absent = {0x40};
    fake_i2c::attempts.clear();
    int attempted = 0, ok = 0;
    // 1.5 s of ticks.
    for (int i = 0; i < 150; i++) {
        const ServoWrite write = tick(controller);
        attempted += write.attempted;
        ok += write.ok;
    }
    // Three in a row, then one 0.5 s after the last and one 0.5 s after that.
    CHECK(attempted == 5);
    CHECK(fake_i2c::attempts[0x40] == 5);
    CHECK(ok == 0);

    fake_i2c::absent.clear();
    int ticksUntilOk = 1;
    while (!tick(controller).ok && ticksUntilOk < 100) ticksUntilOk++;
    CHECK(ticksUntilOk <= 50);
    for (int i = 0; i < 10; i++) CHECK(tick(controller).ok);
    I2CBus::instance().end();
}

// A board that lost power answers again asleep, at its power-on frequency and without auto-increment: it is started
// and woken again, and the next tick's write reaches every channel.
static void aBoardThatLostPowerIsStartedAgain() {
    fake_i2c::reset();
    I2CBus::instance().begin(21, 22);
    static ServoController controller;
    wireReversed(controller);
    controller.activate();
    CHECK(tick(controller).ok);
    fake_i2c::absent = {0x40};
    for (int i = 0; i < 3; i++) CHECK(!tick(controller).ok);
    auto &chip = fake_i2c::registers[0x40];
    chip = {};
    chip[0x00] = 0x11;
    chip[0xFE] = 0x1E;
    fake_i2c::absent.clear();
    while (!tick(controller).ok && fake_timer::nowUs < 100000000) {}
    CHECK(!asleep());
    CHECK(chip[0x00] & 0x20);
    CHECK(chip[0xFE] == 131);
    CHECK(tick(controller).ok);
    for (size_t joint = 0; joint < 12; joint++) CHECK(offCount(REVERSED[joint]) == controller.outputPwm()[joint]);
    I2CBus::instance().end();
}

int main() {
    aBoardThatStopsAnsweringIsTriedTwiceASecond();
    aBoardThatLostPowerIsStartedAgain();
    aSwitchedJointModelIsUsedAndReported();
    withoutAVariantTheServosStayAsleep();
    withAVariantActivatingWakesTheServos();
    allServosMeansEveryMappedChannel();
    eachJointsAngleReachesItsChannel();
    theJointsMoveNoFasterThanTheServos();
    aSavedSupplySlowsTheJointsAtOnce();
    aTargetPastTheTravelIsHeldAtItsEnd();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
