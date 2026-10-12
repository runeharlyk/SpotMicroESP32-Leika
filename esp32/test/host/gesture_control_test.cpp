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
    CHECK(gestureAction(eGestureDown, WALK, State::IDLE, true) == GestureAction::STAND);
    CHECK(gestureAction(eGestureDown, STAND, State::IDLE, true) == GestureAction::SIT);
    CHECK(gestureAction(eGestureDown, ANIMATE, State::HOLD, true) == GestureAction::REST);
    CHECK(gestureAction(eGestureDown, REST, State::IDLE, true) == GestureAction::NONE);
    CHECK(gestureAction(eGestureDown, DEACTIVATED, State::IDLE, true) == GestureAction::REST);
}

static void upStepsBackUp() {
    CHECK(gestureAction(eGestureUp, REST, State::IDLE, true) == GestureAction::STAND);
    CHECK(gestureAction(eGestureUp, ANIMATE, State::HOLD, true) == GestureAction::STAND);
    CHECK(gestureAction(eGestureUp, DEACTIVATED, State::IDLE, true) == GestureAction::STAND);
    CHECK(gestureAction(eGestureUp, STAND, State::IDLE, true) == GestureAction::NONE);
    CHECK(gestureAction(eGestureUp, WALK, State::IDLE, true) == GestureAction::NONE);
}

// A clip on its way in, playing or on its way out is a motion running: the gesture is dropped, not queued.
static void aGestureDuringAMovingClipIsDiscarded() {
    for (State moving : {State::ENTRY, State::PLAYING, State::EXIT}) {
        for (gesture_t gesture : {eGestureDown, eGestureUp, eGestureLeft, eGestureRight}) {
            CHECK(gestureAction(gesture, ANIMATE, moving, true) == GestureAction::NONE);
        }
    }
    CHECK(gestureAction(eGestureLeft, ANIMATE, State::HOLD, true) == GestureAction::NONE);
}

static void withoutASitClipDownFromStandRests() {
    CHECK(gestureAction(eGestureDown, STAND, State::IDLE, false) == GestureAction::REST);
}

static void leftAndRightWalkAndTheRestIsIgnored() {
    CHECK(gestureAction(eGestureLeft, STAND, State::IDLE, true) == GestureAction::WALK);
    CHECK(gestureAction(eGestureRight, REST, State::IDLE, true) == GestureAction::WALK);
    CHECK(gestureAction(eGestureNone, STAND, State::IDLE, true) == GestureAction::NONE);
    CHECK(gestureAction(eGestureClockwise, STAND, State::IDLE, true) == GestureAction::NONE);
    CHECK(gestureAction(eGestureWave, STAND, State::IDLE, true) == GestureAction::NONE);
}

int main() {
    downStepsDownTheLadder();
    upStepsBackUp();
    aGestureDuringAMovingClipIsDiscarded();
    withoutASitClipDownFromStandRests();
    leftAndRightWalkAndTheRestIsIgnored();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
