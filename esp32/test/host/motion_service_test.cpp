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

int main() {
    withoutAVariantEveryModeButDeactivatedIsRefused();
    withAVariantTheRobotStandsOnItsOwnGeometry();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
