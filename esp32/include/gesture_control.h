#pragma once

#include <animation/animation.h>
#include <peripherals/gesture.h>
#include <platform_shared/message.pb.h>

/** What a hand gesture over the sensor asks of the motion. */
enum class GestureAction { NONE, SIT, REST, STAND, WALK };

/**
 * Turns a gesture into a step on the ladder rest - sit - stand: down steps down, up steps back up, and left or right
 * walks. A gesture that arrives while a clip moves the legs (its entry, play or exit) is discarded, not queued, so
 * waving at a robot in motion never stacks up moves. Without a sit clip, down from stand rests as before.
 */
inline GestureAction gestureAction(gesture_t gesture, socket_message_ModesEnum mode, anim::State clipState,
                                   bool canSit) {
    if (gesture == eGestureNone) return GestureAction::NONE;
    const bool animating = mode == socket_message_ModesEnum_ANIMATE;
    if (animating && clipState != anim::State::HOLD) return GestureAction::NONE;
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
        default: return GestureAction::NONE;
    }
}
