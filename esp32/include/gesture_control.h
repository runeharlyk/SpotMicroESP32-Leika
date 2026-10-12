#pragma once

#include <animation/animation.h>
#include <peripherals/gesture.h>
#include <platform_shared/message.pb.h>

/** What a hand gesture over the sensor asks of the motion. */
enum class GestureAction { NONE, SIT, REST, STAND, WALK, TURN_LEFT, TURN_RIGHT, STEP_FORWARD, STEP_BACK };

/** How far a forward or backward gesture walks, and how far a circling one turns. */
constexpr float GESTURE_STEP_M = 0.1f;
constexpr float GESTURE_TURN_RAD = 6.2831853f;

/**
 * Turns a gesture into a step on the ladder rest - sit - stand: down steps down, up steps back up, and left or right
 * walks. From stand or walk, forward and backward walk GESTURE_STEP_M, and anticlockwise and clockwise turn a full
 * circle, as goal moves on the odometry. A gesture that arrives while a motion runs (a clip's entry, play or exit, or
 * a goal) is discarded, not queued, so waving at a robot in motion never stacks up moves. Without a sit clip, down
 * from stand rests as before.
 */
inline GestureAction gestureAction(gesture_t gesture, socket_message_ModesEnum mode, anim::State clipState,
                                   bool canSit, bool goalRunning) {
    if (gesture == eGestureNone || goalRunning) return GestureAction::NONE;
    const bool animating = mode == socket_message_ModesEnum_ANIMATE;
    if (animating && clipState != anim::State::HOLD) return GestureAction::NONE;
    const bool walkable = mode == socket_message_ModesEnum_STAND || mode == socket_message_ModesEnum_WALK;
    switch (gesture) {
        case eGestureDown:
            if (animating) return GestureAction::REST;
            if (mode == socket_message_ModesEnum_STAND) return canSit ? GestureAction::SIT : GestureAction::REST;
            if (mode == socket_message_ModesEnum_WALK) return GestureAction::STAND;
            return mode == socket_message_ModesEnum_REST ? GestureAction::NONE : GestureAction::REST;
        case eGestureUp:
            return mode == socket_message_ModesEnum_STAND || mode == socket_message_ModesEnum_WALK ? GestureAction::NONE
                                                                                                  : GestureAction::STAND;
        case eGestureLeft:
        case eGestureRight: return animating ? GestureAction::NONE : GestureAction::WALK;
        case eGestureForward: return walkable ? GestureAction::STEP_FORWARD : GestureAction::NONE;
        case eGestureBackward: return walkable ? GestureAction::STEP_BACK : GestureAction::NONE;
        case eGestureAntiClockwise: return walkable ? GestureAction::TURN_LEFT : GestureAction::NONE;
        case eGestureClockwise: return walkable ? GestureAction::TURN_RIGHT : GestureAction::NONE;
        default: return GestureAction::NONE;
    }
}
