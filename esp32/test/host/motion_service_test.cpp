// Host test of MotionService's variant gate, built and run by test_host_programs.py.
#include <cmath>
#include <cstdio>
#include <motion.h>
#include <variant.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void tick(MotionService &motion, gesture_t gesture = eGestureNone) {
    fake_timer::nowUs += 10000;
    motion.update(ImuSample {}, gesture);
}

static void withoutAVariantEveryModeButDeactivatedIsRefused() {
    MotionService motion;
    motion.useConfig(kinConfigFor(socket_message_KinematicsVariant_KINEMATICS_VARIANT_UNSET));
    motion.begin();
    for (auto mode : {socket_message_ModesEnum_REST, socket_message_ModesEnum_STAND, socket_message_ModesEnum_WALK}) {
        motion.inbox.postMode(mode);
        tick(motion);
        CHECK(motion.mode() == socket_message_ModesEnum_DEACTIVATED);
        CHECK(!motion.isActive());
        CHECK(!motion.takeModeApplied());
    }
    tick(motion, eGestureUp);
    CHECK(motion.mode() == socket_message_ModesEnum_DEACTIVATED);
    motion.inbox.postMode(socket_message_ModesEnum_DEACTIVATED);
    tick(motion);
    CHECK(motion.takeModeApplied());
}

// The Pico's knee stands at about -90 degrees from the femur, the Leika's at its own; the geometry must be the one
// chosen, not a default.
static void withAVariantTheRobotStandsOnItsOwnGeometry() {
    float knees[2];
    const KinematicsVariant variants[2] = {socket_message_KinematicsVariant_SPOTMICRO_ESP32,
                                           socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI};
    for (int v = 0; v < 2; v++) {
        MotionService motion;
        motion.useConfig(kinConfigFor(variants[v]));
        motion.begin();
        motion.inbox.postMode(socket_message_ModesEnum_STAND);
        for (int i = 0; i < 200; i++) tick(motion);
        CHECK(motion.mode() == socket_message_ModesEnum_STAND);
        CHECK(motion.isActive());
        for (int joint = 0; joint < 12; joint++) CHECK(std::isfinite(motion.getAngles()[joint]));
        knees[v] = motion.getAngles()[2];
    }
    CHECK(std::fabs(knees[0] - knees[1]) > 1);
}

static float standingKnee(MotionService &motion) {
    motion.inbox.postMode(socket_message_ModesEnum_STAND);
    for (int i = 0; i < 200; i++) tick(motion);
    return motion.getAngles()[2];
}

// The knee a robot of `variant` stands with, from a fresh service; the reference for a switched one.
static float kneeOf(KinematicsVariant variant) {
    MotionService motion;
    motion.useConfig(kinConfigFor(variant));
    motion.begin();
    return standingKnee(motion);
}

// After a switch the robot stands exactly as one that booted as the new variant: no geometry is left over.
static void aSwitchedRobotStandsAsTheNewVariant() {
    MotionService motion;
    motion.useConfig(kinConfigFor(socket_message_KinematicsVariant_SPOTMICRO_ESP32));
    motion.begin();
    standingKnee(motion);
    motion.inbox.postMode(socket_message_ModesEnum_DEACTIVATED);
    tick(motion);
    motion.inbox.postVariant(socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI);
    tick(motion);
    CHECK(motion.takeVariantApplied() == socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI);
    CHECK(!motion.takeVariantApplied());
    CHECK(std::fabs(standingKnee(motion) - kneeOf(socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI)) < 1e-4f);
}

// The socket refuses a switch while the legs move, but a mode can arrive in the same tick: the switch then
// deactivates, and the servo controller hears of it through the applied mode.
static void aSwitchArrivingWithAModeDeactivates() {
    MotionService motion;
    motion.useConfig(kinConfigFor(socket_message_KinematicsVariant_SPOTMICRO_ESP32));
    motion.begin();
    motion.inbox.postMode(socket_message_ModesEnum_STAND);
    motion.inbox.postVariant(socket_message_KinematicsVariant_SPOTMICRO_YERTLE);
    tick(motion);
    CHECK(motion.mode() == socket_message_ModesEnum_DEACTIVATED);
    CHECK(!motion.isActive());
    CHECK(motion.takeModeApplied());
    CHECK(motion.takeVariantApplied() == socket_message_KinematicsVariant_SPOTMICRO_YERTLE);
}

// The setup step's case: a robot without a variant is told one and can then stand.
static void aRobotWithoutAVariantCanStandOnceItIsChosen() {
    MotionService motion;
    motion.useConfig(nullptr);
    motion.begin();
    motion.inbox.postVariant(socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI);
    tick(motion);
    motion.inbox.postMode(socket_message_ModesEnum_STAND);
    tick(motion);
    CHECK(motion.mode() == socket_message_ModesEnum_STAND);
}

int main() {
    withoutAVariantEveryModeButDeactivatedIsRefused();
    withAVariantTheRobotStandsOnItsOwnGeometry();
    aSwitchedRobotStandsAsTheNewVariant();
    aSwitchArrivingWithAModeDeactivates();
    aRobotWithoutAVariantCanStandOnceItIsChosen();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
