// Host test of the gesture ladder, built and run by test_host_programs.py.
#include <cstdio>
#include <gesture_control.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

using anim::State;
constexpr auto DEACTIVATED = socket_message_ModesEnum_DEACTIVATED;
constexpr auto REST = socket_message_ModesEnum_REST;
constexpr auto STAND = socket_message_ModesEnum_STAND;
constexpr auto WALK = socket_message_ModesEnum_WALK;
constexpr auto ANIMATE = socket_message_ModesEnum_ANIMATE;

static void downStepsDownTheLadder() {
    CHECK(gestureAction(eGestureDown, WALK, State::IDLE, true, false) == GestureAction::STAND);
    CHECK(gestureAction(eGestureDown, STAND, State::IDLE, true, false) == GestureAction::SIT);
    CHECK(gestureAction(eGestureDown, ANIMATE, State::HOLD, true, false) == GestureAction::REST);
    CHECK(gestureAction(eGestureDown, REST, State::IDLE, true, false) == GestureAction::NONE);
    CHECK(gestureAction(eGestureDown, DEACTIVATED, State::IDLE, true, false) == GestureAction::REST);
}

static void upStepsBackUp() {
    CHECK(gestureAction(eGestureUp, REST, State::IDLE, true, false) == GestureAction::STAND);
    CHECK(gestureAction(eGestureUp, ANIMATE, State::HOLD, true, false) == GestureAction::STAND);
    CHECK(gestureAction(eGestureUp, DEACTIVATED, State::IDLE, true, false) == GestureAction::STAND);
    CHECK(gestureAction(eGestureUp, STAND, State::IDLE, true, false) == GestureAction::NONE);
    CHECK(gestureAction(eGestureUp, WALK, State::IDLE, true, false) == GestureAction::NONE);
}

// A clip on its way in, playing or on its way out is a motion running: the gesture is dropped, not queued.
static void aGestureDuringAMovingClipIsDiscarded() {
    for (State moving : {State::ENTRY, State::PLAYING, State::EXIT}) {
        for (gesture_t gesture : {eGestureDown, eGestureUp, eGestureLeft, eGestureRight}) {
            CHECK(gestureAction(gesture, ANIMATE, moving, true, false) == GestureAction::NONE);
        }
    }
    CHECK(gestureAction(eGestureLeft, ANIMATE, State::HOLD, true, false) == GestureAction::NONE);
}

static void withoutASitClipDownFromStandRests() {
    CHECK(gestureAction(eGestureDown, STAND, State::IDLE, false, false) == GestureAction::REST);
}

static void leftAndRightWalkAndTheRestIsIgnored() {
    CHECK(gestureAction(eGestureLeft, STAND, State::IDLE, true, false) == GestureAction::WALK);
    CHECK(gestureAction(eGestureRight, REST, State::IDLE, true, false) == GestureAction::WALK);
    CHECK(gestureAction(eGestureNone, STAND, State::IDLE, true, false) == GestureAction::NONE);
    CHECK(gestureAction(eGestureWave, STAND, State::IDLE, true, false) == GestureAction::NONE);
}

// From stand or walk the circling gestures turn and the forward and backward ones step, as goal moves.
static void circlingTurnsAndForwardOrBackwardSteps() {
    for (auto mode : {STAND, WALK}) {
        CHECK(gestureAction(eGestureAntiClockwise, mode, State::IDLE, true, false) == GestureAction::TURN_LEFT);
        CHECK(gestureAction(eGestureClockwise, mode, State::IDLE, true, false) == GestureAction::TURN_RIGHT);
        CHECK(gestureAction(eGestureForward, mode, State::IDLE, true, false) == GestureAction::STEP_FORWARD);
        CHECK(gestureAction(eGestureBackward, mode, State::IDLE, true, false) == GestureAction::STEP_BACK);
    }
    // A goal walks from stand or walk only: lying down or sitting, the robot ignores them.
    for (auto mode : {REST, DEACTIVATED})
        for (gesture_t gesture : {eGestureClockwise, eGestureAntiClockwise, eGestureForward, eGestureBackward})
            CHECK(gestureAction(gesture, mode, State::IDLE, true, false) == GestureAction::NONE);
    CHECK(gestureAction(eGestureForward, ANIMATE, State::HOLD, true, false) == GestureAction::NONE);
}

// A goal running is a motion running: every gesture waits for it to end, none is queued behind it.
static void aGestureDuringAGoalIsDiscarded() {
    for (gesture_t gesture : {eGestureDown, eGestureUp, eGestureLeft, eGestureForward, eGestureClockwise})
        CHECK(gestureAction(gesture, WALK, State::IDLE, true, true) == GestureAction::NONE);
}

int main() {
    circlingTurnsAndForwardOrBackwardSteps();
    aGestureDuringAGoalIsDiscarded();
    downStepsDownTheLadder();
    upStepsBackUp();
    aGestureDuringAMovingClipIsDiscarded();
    withoutASitClipDownFromStandRests();
    leftAndRightWalkAndTheRestIsIgnored();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
