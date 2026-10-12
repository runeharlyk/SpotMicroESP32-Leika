// Host test of MotionService's variant gate, built and run by test_host_programs.py.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
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

static ImuSample tilted(float degrees) {
    ImuSample imu;
    imu.valid = ImuValid::ACCEL | ImuValid::GYRO | ImuValid::ORIENTATION;
    const float rad = degrees * 3.14159265f / 180.0f;
    imu.gravity = {0, std::sin(rad), -std::cos(rad)};  // rolled about x
    return imu;
}

static void standPico(MotionService &motion) {
    motion.useConfig(kinConfigFor(socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI));
    motion.begin();
    motion.inbox.postMode(socket_message_ModesEnum_STAND);
    tick(motion);
    motion.takeModeApplied();
}

// On its side, servos pushing against the floor stall and drain the battery: the robot lets go, and says so through
// the mode change the control loop turns into a sleeping servo board.
static void aRobotLyingOnItsSideDeactivates() {
    MotionService motion;
    standPico(motion);
    for (int i = 0; i < 29; i++) {
        fake_timer::nowUs += 10000;
        motion.update(tilted(80), eGestureNone);
    }
    CHECK(motion.mode() == socket_message_ModesEnum_STAND);  // 290 ms: a stumble may still right itself
    fake_timer::nowUs += 20000;
    motion.update(tilted(80), eGestureNone);
    CHECK(motion.mode() == socket_message_ModesEnum_DEACTIVATED);
    CHECK(!motion.isActive());
    CHECK(motion.takeModeApplied());
}

// A slope or the stand's own levelling tilts the body far less; a brief knock past the limit does not add up.
static void aTiltedOrKnockedRobotKeepsStanding() {
    MotionService motion;
    standPico(motion);
    for (int i = 0; i < 300; i++) {
        fake_timer::nowUs += 10000;
        motion.update(tilted(i % 25 < 20 ? 35 : 90), eGestureNone);
    }
    CHECK(motion.mode() == socket_message_ModesEnum_STAND);
    for (int i = 0; i < 100; i++) {
        fake_timer::nowUs += 10000;
        motion.update(ImuSample {}, eGestureNone);  // no IMU: nothing to tell a fall by
    }
    CHECK(motion.mode() == socket_message_ModesEnum_STAND);
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

// This firmware carries no policy: WALK_NN is refused even on the Mini with a working IMU, and the robot stays as it was.
static void withoutAPolicyWalkNNIsRefused() {
    static_assert(!WALK_NN_POLICY, "built without a policy header");
    MotionService motion;
    motion.useConfig(kinConfigFor(socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI));
    motion.begin();
    CHECK(!motion.walkNNAvailable());
    motion.inbox.postMode(socket_message_ModesEnum_STAND);
    tick(motion);
    CHECK(motion.takeModeApplied());
    ImuSample imu;
    imu.valid = ImuValid::ACCEL | ImuValid::GYRO | ImuValid::ORIENTATION;
    motion.inbox.postMode(socket_message_ModesEnum_WALK_NN);
    fake_timer::nowUs += 10000;
    motion.update(imu, eGestureNone);
    CHECK(motion.mode() == socket_message_ModesEnum_STAND);
    CHECK(!motion.takeModeApplied());
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

static socket_message_ControllerData forward() {
    socket_message_ControllerData data = socket_message_ControllerData_init_zero;
    data.has_left = data.has_right = true;
    data.left.y = 1;
    data.height = 0.5f;
    data.speed = 0.5f;
    return data;
}

// The largest change of any joint's commanded angle from one tick to the next, over `ticks` ticks.
static float largestJointStep(MotionService &motion, int ticks) {
    float largest = 0;
    float previous[12];
    std::copy(motion.getAngles(), motion.getAngles() + 12, previous);
    for (int i = 0; i < ticks; i++) {
        tick(motion);
        for (int joint = 0; joint < 12; joint++) {
            largest = std::max(largest, std::fabs(motion.getAngles()[joint] - previous[joint]));
            previous[joint] = motion.getAngles()[joint];
        }
    }
    return largest;
}

// The firmware traces showed walk to stand jumping a joint 16.8 degrees in one tick: the feet snapped from mid-stride
// to their stand positions. Switched at several points of the gait, so a foot is in the air at least once.
static void leavingAWalkMovesTheLegsSmoothly() {
    const KinematicsVariant pico = socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI;
    const float standKnee = kneeOf(pico);
    float worst = 0;
    for (int phase = 0; phase < 6; phase++) {
        MotionService motion;
        motion.useConfig(kinConfigFor(pico));
        motion.begin();
        motion.inbox.postMode(socket_message_ModesEnum_STAND);
        for (int i = 0; i < 100; i++) tick(motion);
        motion.inbox.postMode(socket_message_ModesEnum_WALK);
        for (int i = 0; i < 150 + 7 * phase; i++) {
            motion.inbox.postInput(forward(), fake_timer::nowUs / 1000, fake_timer::nowUs);
            tick(motion);
        }
        motion.inbox.postMode(socket_message_ModesEnum_STAND);
        worst = std::max(worst, largestJointStep(motion, 100));
        for (int i = 0; i < 100; i++) tick(motion);
        // Within update_angles' 0.1 degree deadband, which an eased approach can stop short by.
        CHECK(std::fabs(motion.getAngles()[2] - standKnee) <= 0.1f);
    }
    if (worst >= 3.0f) std::printf("largest joint step after walk to stand: %.2f deg\n", worst);
    CHECK(worst < 3.0f);
}

static socket_message_ControllerData stick(float lx, float ly, float rx, float ry) {
    socket_message_ControllerData data = socket_message_ControllerData_init_zero;
    data.has_left = data.has_right = true;
    data.left = {lx, ly};
    data.right = {rx, ry};
    data.height = 0.5f;
    data.speed = 0.5f;
    data.s1 = 0.5f;
    return data;
}

// The servos hold every joint inside its travel (joint_model.h); rest, stand and the walk in every direction, in both
// gaits, must not need that, or the robot would move differently than its gait says. The walk is at 0.7 of full
// stick: walking backward, the Pico asks its front left femur for 51 degrees at 0.8 and 67 at full stick, past the
// servo's travel.
static void everyModeStaysInsideTheJointTravel() {
    constexpr float WALK = 0.7f;
    for (KinematicsVariant variant :
         {socket_message_KinematicsVariant_SPOTMICRO_ESP32, socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI,
          socket_message_KinematicsVariant_SPOTMICRO_YERTLE}) {
        const JointModel &model = *jointModelFor(variant);
        MotionService motion;
        motion.useConfig(kinConfigFor(variant));
        motion.begin();
        int outside = 0;
        auto run = [&](socket_message_ModesEnum mode, socket_message_ControllerData input, int ticks) {
            motion.inbox.postMode(mode);
            for (int i = 0; i < ticks; i++) {
                motion.inbox.postInput(input, fake_timer::nowUs / 1000, fake_timer::nowUs);
                tick(motion);
                for (size_t joint = 0; joint < 12; joint++)
                    if (!withinTravel(model, joint, motion.getAngles()[joint] * JOINT_DIRECTION[joint])) outside++;
            }
        };
        run(socket_message_ModesEnum_REST, stick(0, 0, 0, 0), 100);
        run(socket_message_ModesEnum_STAND, stick(1, -1, 1, -1), 100);
        run(socket_message_ModesEnum_STAND, stick(-1, 1, -1, 1), 100);
        for (auto gait : {socket_message_WalkGaits_TROT, socket_message_WalkGaits_CRAWL}) {
            motion.inbox.postGait(gait);
            for (auto input : {stick(0, WALK, 0, 0), stick(0, -WALK, 0, 0), stick(WALK, 0, 0, 0), stick(-WALK, 0, 0, 0),
                               stick(0, 0, WALK, 0), stick(0, 0, -WALK, 0)})
                run(socket_message_ModesEnum_WALK, input, 200);
        }
        run(socket_message_ModesEnum_REST, stick(0, 0, 0, 0), 100);
        if (outside) std::printf("%s: %d joint commands outside the travel\n", variantName(variant), outside);
        CHECK(outside == 0);
    }
}

static MotionInbox::Play bob() {
    auto clip = std::make_shared<anim::Clip>();
    std::strcpy(clip->name, "bob");
    clip->keyframeCount = 2;
    clip->keyframes[1].time = 1;
    clip->keyframes[1].body[anim::Z] = 10;
    return {clip, {}, 0};
}

static std::shared_ptr<const anim::Clip> crouchAndHold() {
    auto clip = std::make_shared<anim::Clip>();
    std::strcpy(clip->name, "crouch");
    clip->keyframeCount = 2;
    clip->keyframes[1].time = 0.5f;
    clip->keyframes[1].body[anim::Z] = -15;
    clip->holdEnd = true;
    return clip;
}

static void standAsPico(MotionService &motion);

// Down from stand sits and holds; a gesture while the clip still moves is dropped; down again rests through the exit.
static void gesturesSitThenRest() {
    MotionService motion;
    motion.setSitClip(crouchAndHold());
    standAsPico(motion);
    tick(motion, eGestureDown);
    CHECK(motion.mode() == socket_message_ModesEnum_ANIMATE);
    tick(motion, eGestureUp);  // still entering: discarded
    for (int i = 0; i < 150; i++) tick(motion);
    CHECK(motion.mode() == socket_message_ModesEnum_ANIMATE);
    CHECK(motion.animationStatus().state == anim::State::HOLD);
    tick(motion, eGestureDown);
    for (int i = 0; i < 100; i++) tick(motion);
    CHECK(motion.mode() == socket_message_ModesEnum_REST);
    tick(motion, eGestureUp);
    CHECK(motion.mode() == socket_message_ModesEnum_STAND);
}

static void standAsPico(MotionService &motion) {
    motion.useConfig(kinConfigFor(socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI));
    motion.begin();
    motion.inbox.postMode(socket_message_ModesEnum_STAND);
    for (int i = 0; i < 100; i++) tick(motion);
}

// From stand into the clip and back to stand without a jump: entry, half a second; the clip, a second; exit, half.
static void aClipPlaysFromStandAndHandsBackToStand() {
    MotionService motion;
    standAsPico(motion);
    const float standKnee = motion.getAngles()[2];
    motion.inbox.postPlay(bob());
    tick(motion);
    CHECK(motion.mode() == socket_message_ModesEnum_ANIMATE);
    CHECK(motion.takeModeApplied() && motion.isActive());
    CHECK(std::strcmp(motion.animationStatus().name, "bob") == 0);
    CHECK(largestJointStep(motion, 220) < 3.0f);
    CHECK(motion.mode() == socket_message_ModesEnum_STAND);
    CHECK(motion.animationStatus().state == anim::State::IDLE);
    CHECK(std::fabs(motion.getAngles()[2] - standKnee) <= 0.1f);
}

static void aClipStartedWhileWalkingHandsBackToStand() {
    MotionService motion;
    standAsPico(motion);
    motion.inbox.postMode(socket_message_ModesEnum_WALK);
    tick(motion);
    motion.inbox.postPlay(bob());
    for (int i = 0; i < 300; i++) tick(motion);
    CHECK(motion.mode() == socket_message_ModesEnum_STAND);
}

static void aDeactivatedRobotPlaysNothing() {
    MotionService motion;
    motion.useConfig(kinConfigFor(socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI));
    motion.begin();
    motion.inbox.postPlay(bob());
    tick(motion);
    CHECK(motion.mode() == socket_message_ModesEnum_DEACTIVATED && !motion.isActive());
}

// Another mode waits for the clip's exit; deactivating does not wait.
static void aModeAskedForDuringAClipFollowsItsExit() {
    MotionService motion;
    standAsPico(motion);
    motion.inbox.postPlay(bob());
    for (int i = 0; i < 80; i++) tick(motion);
    motion.inbox.postMode(socket_message_ModesEnum_REST);
    tick(motion);
    CHECK(motion.mode() == socket_message_ModesEnum_ANIMATE);
    CHECK(motion.animationStatus().state == anim::State::EXIT);
    for (int i = 0; i < 60; i++) tick(motion);
    CHECK(motion.mode() == socket_message_ModesEnum_REST);

    motion.inbox.postPlay(bob());
    for (int i = 0; i < 80; i++) tick(motion);
    motion.inbox.postMode(socket_message_ModesEnum_DEACTIVATED);
    tick(motion);
    CHECK(motion.mode() == socket_message_ModesEnum_DEACTIVATED && !motion.isActive());
    CHECK(motion.animationStatus().state == anim::State::IDLE);
}

// The servo angles a pose stands for on the Pico at this body height, as AnimateState converts it.
static void poseAngles(const anim::Pose &pose, float base, float out[12]) {
    const KinConfig &config = *kinConfigFor(socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI);
    Kinematics kin(config);
    float ik[12];
    anim::poseToAngles(pose, kin, config.default_feet_positions, base, ik);
    for (int joint = 0; joint < 12; joint++) out[joint] = ik[joint] * JOINT_DIRECTION[joint];
}

static float largestOffFrom(const MotionService &motion, const float angles[12]) {
    float largest = 0;
    for (int joint = 0; joint < 12; joint++)
        largest = std::max(largest, std::fabs(motion.getAngles()[joint] - angles[joint]));
    return largest;
}

static anim::Pose crouchLiftingAFoot() {
    anim::Pose pose;
    pose.body[anim::Z] = -15;
    pose.body[anim::PITCH] = 0.1f;
    pose.legs[0].v[2] = 20;
    return pose;
}

// The editor's pose takes the robot from stand into ANIMATE, the legs ease to it without a jump and hold it with no
// stream to keep it alive, and a stop eases them home and hands stand back.
static void aShownPoseIsHeldAndEasedHomeOnStop() {
    MotionService motion;
    standAsPico(motion);
    const float base = KIN_CONFIG_SPOTMICRO_ESP32_MINI.min_body_height + 0.5f * KIN_CONFIG_SPOTMICRO_ESP32_MINI.body_height_range;
    float stand[12], held[12];
    std::copy(motion.getAngles(), motion.getAngles() + 12, stand);
    poseAngles(crouchLiftingAFoot(), base, held);
    motion.inbox.postPose(crouchLiftingAFoot(), 3);
    CHECK(largestJointStep(motion, 150) < 3.0f);
    CHECK(motion.mode() == socket_message_ModesEnum_ANIMATE);
    CHECK(largestOffFrom(motion, held) <= 0.1f);
    for (int i = 0; i < 300; i++) tick(motion);
    CHECK(motion.mode() == socket_message_ModesEnum_ANIMATE);
    CHECK(largestOffFrom(motion, held) <= 0.1f);
    motion.inbox.postStopAnimation();
    CHECK(largestJointStep(motion, 200) < 3.0f);
    CHECK(motion.mode() == socket_message_ModesEnum_STAND);
    CHECK(largestOffFrom(motion, stand) <= 0.2f);
}

// A leg the editor turns from a foot into joint angles and back moves there without a jump, and holds each.
static void aLegChangingBetweenAFootAndJointsDoesNotJump() {
    MotionService motion;
    standAsPico(motion);
    const float base = KIN_CONFIG_SPOTMICRO_ESP32_MINI.min_body_height + 0.5f * KIN_CONFIG_SPOTMICRO_ESP32_MINI.body_height_range;
    anim::Pose joints = crouchLiftingAFoot();
    joints.legs[2] = {true, {5, -30, 80}};
    float target[12];
    motion.inbox.postPose(crouchLiftingAFoot());
    for (int i = 0; i < 150; i++) tick(motion);
    motion.inbox.postPose(joints);
    CHECK(largestJointStep(motion, 150) < 3.0f);
    poseAngles(joints, base, target);
    CHECK(largestOffFrom(motion, target) <= 0.1f);
    anim::Pose feet = crouchLiftingAFoot();
    feet.legs[2].v[0] = 15;
    motion.inbox.postPose(feet);
    CHECK(largestJointStep(motion, 200) < 3.0f);
    poseAngles(feet, base, target);
    CHECK(largestOffFrom(motion, target) <= 0.1f);
}

// Its client leaving eases the pose home, as a stop would: nobody else knows the robot holds it.
static void aShownPoseEasesHomeWhenItsClientLeaves() {
    MotionService motion;
    standAsPico(motion);
    motion.inbox.postPose(crouchLiftingAFoot(), 3);
    for (int i = 0; i < 100; i++) tick(motion);
    motion.inbox.dropClient(3);
    CHECK(largestJointStep(motion, 200) < 3.0f);
    CHECK(motion.mode() == socket_message_ModesEnum_STAND);
}

// A pose moves no leg that is not under power and takes no robot out of another mode than stand, and does not cut into
// a clip; a mode asked for while a pose is held wins over poses still on their way, which the editor sends as a slider
// moves.
static void aShownPoseIsRefusedUnpoweredDuringAClipAndAfterAMode() {
    {
        MotionService motion;
        motion.useConfig(kinConfigFor(socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI));
        motion.begin();
        motion.inbox.postPose(crouchLiftingAFoot());
        tick(motion);
        CHECK(motion.mode() == socket_message_ModesEnum_DEACTIVATED && !motion.isActive());
    }
    {
        MotionService motion;
        standAsPico(motion);
        motion.inbox.postPlay(bob());
        for (int i = 0; i < 80; i++) tick(motion);
        motion.inbox.postPose(crouchLiftingAFoot());
        tick(motion);
        CHECK(motion.animationStatus().state == anim::State::PLAYING);
        for (int i = 0; i < 150; i++) tick(motion);
        CHECK(motion.mode() == socket_message_ModesEnum_STAND);
    }
    {
        MotionService motion;
        standAsPico(motion);
        motion.inbox.postPose(crouchLiftingAFoot());
        for (int i = 0; i < 100; i++) tick(motion);
        motion.inbox.postMode(socket_message_ModesEnum_REST);
        tick(motion);
        for (int i = 0; i < 200; i++) {
            motion.inbox.postPose(crouchLiftingAFoot());
            tick(motion);
        }
        CHECK(motion.mode() == socket_message_ModesEnum_REST);
    }
    {
        MotionService motion;
        standAsPico(motion);
        motion.inbox.postPose(crouchLiftingAFoot());
        for (int i = 0; i < 100; i++) tick(motion);
        motion.inbox.postMode(socket_message_ModesEnum_DEACTIVATED);
        tick(motion);
        CHECK(motion.mode() == socket_message_ModesEnum_DEACTIVATED && !motion.isActive());
    }
}

int main() {
    aShownPoseIsHeldAndEasedHomeOnStop();
    aLegChangingBetweenAFootAndJointsDoesNotJump();
    aShownPoseEasesHomeWhenItsClientLeaves();
    aShownPoseIsRefusedUnpoweredDuringAClipAndAfterAMode();
    gesturesSitThenRest();
    aClipPlaysFromStandAndHandsBackToStand();
    aClipStartedWhileWalkingHandsBackToStand();
    aDeactivatedRobotPlaysNothing();
    aModeAskedForDuringAClipFollowsItsExit();
    aRobotLyingOnItsSideDeactivates();
    aTiltedOrKnockedRobotKeepsStanding();
    leavingAWalkMovesTheLegsSmoothly();
    withoutAVariantEveryModeButDeactivatedIsRefused();
    withAVariantTheRobotStandsOnItsOwnGeometry();
    aSwitchedRobotStandsAsTheNewVariant();
    aSwitchArrivingWithAModeDeactivates();
    aRobotWithoutAVariantCanStandOnceItIsChosen();
    everyModeStaysInsideTheJointTravel();
    withoutAPolicyWalkNNIsRefused();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
