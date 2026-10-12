// Host test of communication/handheld_controller.h, built and run by test_host_programs.py.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <communication/handheld_controller.h>
#include <message_types.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static constexpr auto DEACTIVATED = socket_message_ModesEnum_DEACTIVATED;
static constexpr auto IDLE = socket_message_ModesEnum_IDLE;
static constexpr auto CALIBRATION = socket_message_ModesEnum_CALIBRATION;
static constexpr auto REST = socket_message_ModesEnum_REST;
static constexpr auto STAND = socket_message_ModesEnum_STAND;
static constexpr auto WALK = socket_message_ModesEnum_WALK;
static constexpr auto TROT = socket_message_WalkGaits_TROT;
static constexpr auto CRAWL = socket_message_WalkGaits_CRAWL;

static controller_packet_t packet(uint8_t buttons = 0, int16_t lx = 0, int16_t ly = 0, int16_t rx = 0,
                                  int16_t ry = 0) {
    return {CONTROLLER_PACKET_VERSION, buttons, lx, ly, rx, ry, 0};
}

static bool near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

static CommandMsg command(const socket_message_ControllerData &input) {
    CommandMsg msg;
    msg.fromProto(input);
    return msg;
}

// The wire format the controller firmware sends: 14 packed little-endian bytes.
static void theFrameDecodesAsTheControllerLaysItOut() {
    CHECK(sizeof(controller_packet_t) == 14);
    CHECK(offsetof(controller_packet_t, buttons) == 1);
    CHECK(offsetof(controller_packet_t, left_x) == 2);
    CHECK(offsetof(controller_packet_t, right_y) == 8);
    CHECK(offsetof(controller_packet_t, seq) == 10);

    const uint8_t frame[14] = {CONTROLLER_PACKET_VERSION, BTN_LEFT | BTN_C, 0xE8, 0x03, 0x18, 0xFC, 0xF4, 0x01,
                               0x0C, 0xFE, 0x78, 0x56, 0x34, 0x12};
    const auto decoded = HandheldController::decode(frame, sizeof(frame));
    CHECK(decoded.has_value());
    CHECK(decoded->buttons == (BTN_LEFT | BTN_C));
    CHECK(decoded->left_x == 1000);
    CHECK(decoded->left_y == -1000);
    CHECK(decoded->right_x == 500);
    CHECK(decoded->right_y == -500);
    CHECK(decoded->seq == 0x12345678u);
}

static void aFrameOfAnotherVersionOrLengthIsRefused() {
    uint8_t frame[15] = {CONTROLLER_PACKET_VERSION};
    CHECK(HandheldController::decode(frame, 14).has_value());
    CHECK(!HandheldController::decode(frame, 13).has_value());
    CHECK(!HandheldController::decode(frame, 15).has_value());
    CHECK(!HandheldController::decode(frame, 0).has_value());
    CHECK(!HandheldController::decode(frame, -1).has_value());
    frame[0] = CONTROLLER_PACKET_VERSION - 1;
    CHECK(!HandheldController::decode(frame, 14).has_value());
    frame[0] = CONTROLLER_PACKET_VERSION + 1;
    CHECK(!HandheldController::decode(frame, 14).has_value());
}

// The robot's own beacon is broadcast on the same channel and must never be read as a controller frame.
static void theBeaconIsFourBytesAndNeverAControllerFrame() {
    CHECK(sizeof(hexapod_beacon_t) == 4);
    const hexapod_beacon_t beacon = beaconFor(11);
    uint8_t bytes[4];
    std::memcpy(bytes, &beacon, sizeof(bytes));
    CHECK(bytes[0] == 0xA5);
    CHECK(bytes[1] == 1);
    CHECK(bytes[2] == 11);
    CHECK(bytes[3] == 0);
    CHECK(!HandheldController::decode(bytes, sizeof(bytes)).has_value());
}

static void theSticksScaleToTheAppsRangeAndTheSlidersToItsDefaults() {
    HandheldController controller;
    const auto actions = controller.handle(packet(0, 1000, -500, 250, -1000), WALK, TROT);
    CHECK(actions.input.has_value());
    const CommandMsg msg = command(*actions.input);
    CHECK(msg.lx == 1.0f);
    CHECK(near(msg.ly, -0.5f));
    CHECK(near(msg.rx, 0.25f));
    CHECK(msg.ry == -1.0f);
    CHECK(msg.h == HandheldController::DEFAULT_HEIGHT);
    CHECK(msg.s == HandheldController::SPEED);
    CHECK(msg.s1 == HandheldController::STEP_HEIGHT);
    CHECK(!actions.mode && !actions.gait);
}

// The packet allows the whole int16 range; the inbox bounds what it gets, as for the app.
static void anAxisBeyondFullScaleIsBounded() {
    HandheldController controller;
    const CommandMsg msg = command(*controller.handle(packet(0, INT16_MIN, INT16_MAX), WALK, TROT).input);
    CHECK(msg.lx == -1.0f);
    CHECK(msg.ly == 1.0f);
}

static void inputIsPostedEveryFrameWhileSteeringAndOnlyOnChangeAtRest() {
    HandheldController controller;
    CHECK(controller.handle(packet(), WALK, TROT).input);  // the first frame sets the controller's sliders
    CHECK(!controller.handle(packet(), WALK, TROT).input); // an idle heartbeat leaves the app's input alone
    CHECK(controller.handle(packet(0, 0, 400), WALK, TROT).input);
    CHECK(controller.handle(packet(0, 0, 400), WALK, TROT).input); // the same deflection keeps the dead-man alive
    const auto centred = controller.handle(packet(), WALK, TROT);
    CHECK(centred.input && !command(*centred.input).steering()); // releasing the stick stops the robot
    CHECK(!controller.handle(packet(), WALK, TROT).input);
}

static void theLeftButtonStepsThroughTheModesOnItsPress() {
    HandheldController controller;
    CHECK(controller.handle(packet(BTN_LEFT), DEACTIVATED, TROT).mode == REST);
    CHECK(!controller.handle(packet(BTN_LEFT), REST, TROT).mode); // held, not pressed again
    controller.handle(packet(), REST, TROT);
    CHECK(controller.handle(packet(BTN_LEFT), REST, TROT).mode == STAND);
    controller.handle(packet(), STAND, TROT);
    CHECK(controller.handle(packet(BTN_LEFT), STAND, TROT).mode == WALK);
    controller.handle(packet(), WALK, TROT);
    CHECK(controller.handle(packet(BTN_LEFT), WALK, TROT).mode == DEACTIVATED);
}

// The cycle follows the mode the robot is in, whoever set it, and a mode outside it steps on as DEACTIVATED does.
static void theNextModeFollowsTheRobotsMode() {
    for (auto mode : {IDLE, CALIBRATION}) {
        HandheldController controller;
        CHECK(controller.handle(packet(BTN_LEFT), mode, TROT).mode == REST);
    }
    HandheldController controller;
    CHECK(controller.handle(packet(BTN_LEFT), WALK, TROT).mode == DEACTIVATED);
}

static void theRightButtonTogglesTheGaitOutsideStand() {
    HandheldController controller;
    CHECK(controller.handle(packet(BTN_RIGHT), WALK, TROT).gait == CRAWL);
    CHECK(!controller.handle(packet(BTN_RIGHT), WALK, CRAWL).gait);
    controller.handle(packet(), WALK, CRAWL);
    CHECK(controller.handle(packet(BTN_RIGHT), WALK, CRAWL).gait == TROT);
    controller.handle(packet(), REST, TROT);
    CHECK(controller.handle(packet(BTN_RIGHT), REST, TROT).gait == CRAWL);
    controller.handle(packet(), STAND, TROT);
    CHECK(!controller.handle(packet(BTN_RIGHT), STAND, TROT).gait);
}

static void inStandTheRightButtonAndStickSetAHeightThatStays() {
    HandheldController controller;
    auto actions = controller.handle(packet(BTN_RIGHT, 300, 0, 0, 1000), STAND, TROT);
    CommandMsg msg = command(*actions.input);
    CHECK(msg.h == 1.0f);
    CHECK(msg.ry == 0.0f);  // the stick sets the height, not the pitch
    CHECK(near(msg.lx, 0.3f));  // the other sticks still pose the body
    msg = command(*controller.handle(packet(BTN_RIGHT, 0, 0, 0, -1000), STAND, TROT).input);
    CHECK(msg.h == 0.0f);
    msg = command(*controller.handle(packet(BTN_RIGHT, 0, 0, 0, -500), STAND, TROT).input);
    CHECK(near(msg.h, 0.35f));
    msg = command(*controller.handle(packet(BTN_RIGHT), STAND, TROT).input);
    CHECK(msg.h == HandheldController::DEFAULT_HEIGHT);  // centred is the default height

    controller.handle(packet(BTN_RIGHT, 0, 0, 0, 500), STAND, TROT);
    msg = command(*controller.handle(packet(0, 0, 0, 0, 500), STAND, TROT).input);
    CHECK(near(msg.h, 0.85f));  // released, the height stays and the stick pitches the body again
    CHECK(near(msg.ry, 0.5f));
    msg = command(*controller.handle(packet(0, 0, 1000), WALK, TROT).input);
    CHECK(near(msg.h, 0.85f));  // and it carries into walking
}

static void outsideStandTheRightStickIsNeverTheHeight() {
    HandheldController controller;
    const CommandMsg msg = command(*controller.handle(packet(BTN_RIGHT, 0, 0, 0, 1000), WALK, TROT).input);
    CHECK(msg.ry == 1.0f);
    CHECK(msg.h == HandheldController::DEFAULT_HEIGHT);
}

static void bothButtonsDeactivateAndDoNothingElse() {
    HandheldController controller;
    controller.handle(packet(BTN_LEFT), WALK, TROT);
    auto actions = controller.handle(packet(BTN_LEFT | BTN_RIGHT), WALK, TROT);
    CHECK(actions.mode == DEACTIVATED);
    CHECK(!actions.gait); // the right button's press is the stop, not a gait change
    actions = controller.handle(packet(BTN_LEFT | BTN_RIGHT), DEACTIVATED, TROT);
    CHECK(!actions.mode); // held, not pressed again
    actions = controller.handle(packet(BTN_LEFT), DEACTIVATED, TROT);
    CHECK(!actions.mode && !actions.gait); // letting go of one is no press of the other
    actions = controller.handle(packet(BTN_LEFT | BTN_RIGHT), STAND, TROT);
    CHECK(actions.mode == DEACTIVATED);
}

static void bothButtonsStillPassTheSticks() {
    HandheldController controller;
    const auto actions = controller.handle(packet(BTN_LEFT | BTN_RIGHT, 0, 700), WALK, TROT);
    CHECK(actions.input && near(command(*actions.input).ly, 0.7f));
}

static void theAuxiliaryButtonsAreUnmapped() {
    HandheldController controller;
    controller.handle(packet(), WALK, TROT);
    const auto actions = controller.handle(packet(BTN_A | BTN_B | BTN_C), WALK, TROT);
    CHECK(!actions.mode && !actions.gait && !actions.input);
}

// Broadcasts carry no pairing: the first controller claims the robot, and neither a second controller nor another
// ESP-NOW device's frame takes it over until the receiver restarts.
static void theFirstControllerHeardKeepsTheRobot() {
    const uint8_t first[6] = {1, 2, 3, 4, 5, 6}, second[6] = {1, 2, 3, 4, 5, 7}, other[6] = {9, 9, 9, 9, 9, 9};
    uint8_t frame[14] = {CONTROLLER_PACKET_VERSION};
    const uint8_t beaconLike[4] = {HEXAPOD_BEACON_MAGIC, HEXAPOD_BEACON_VERSION, 6, 0};
    ControllerLock lock;
    CHECK(!lock.admit(other, beaconLike, sizeof(beaconLike)));  // not a controller frame: claims nothing
    CHECK(lock.admit(first, frame, sizeof(frame)));
    CHECK(!lock.admit(second, frame, sizeof(frame)));
    CHECK(!lock.admit(other, frame, sizeof(frame)));
    CHECK(lock.admit(first, frame, sizeof(frame)));
    lock.reset();
    CHECK(lock.admit(second, frame, sizeof(frame)));
    CHECK(!lock.admit(first, frame, sizeof(frame)));
}

int main() {
    theFirstControllerHeardKeepsTheRobot();
    theFrameDecodesAsTheControllerLaysItOut();
    aFrameOfAnotherVersionOrLengthIsRefused();
    theBeaconIsFourBytesAndNeverAControllerFrame();
    theSticksScaleToTheAppsRangeAndTheSlidersToItsDefaults();
    anAxisBeyondFullScaleIsBounded();
    inputIsPostedEveryFrameWhileSteeringAndOnlyOnChangeAtRest();
    theLeftButtonStepsThroughTheModesOnItsPress();
    theNextModeFollowsTheRobotsMode();
    theRightButtonTogglesTheGaitOutsideStand();
    inStandTheRightButtonAndStickSetAHeightThatStays();
    outsideStandTheRightStickIsNeverTheHeight();
    bothButtonsDeactivateAndDoNothingElse();
    bothButtonsStillPassTheSticks();
    theAuxiliaryButtonsAreUnmapped();
    if (failures == 0) std::printf("all handheld controller tests passed\n");
    return failures == 0 ? 0 : 1;
}
