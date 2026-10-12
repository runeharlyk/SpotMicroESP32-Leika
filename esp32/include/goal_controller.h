#pragma once

#include <odometry.h>
#include <utils/finite.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

/** A move the robot makes on its own: a distance forward, a distance sideways or a turn, never two at once. */
struct Goal {
    float forward = 0;  // m, REP-103
    float sideways = 0; // m, to the left
    float turn = 0;     // rad, counter-clockwise
    float speed = 0.5f; // of the gait's top, in (0, 1]
};

// The values of the protocol's GoalState.
enum class GoalState : uint8_t { IDLE, RUNNING, REACHED, CANCELLED, TIMED_OUT };

/** Stick deflections from -1 to 1 of the gait's top: forward, to the left and counter-clockwise. */
struct GoalSticks {
    float forward = 0, left = 0, turn = 0;
};

/**
 * Drives one goal to its target on the body's own motion, fed one step per control tick: the remaining distance along
 * the target's axis in the frame the goal started in, or the remaining turn, sets the stick. It slows in proportion
 * over the last SLOW_DOWN, down to MIN_FRACTION of the asked speed, and walks on until it reaches the target: stopping
 * at the edge of the tolerance would leave it there, since the command filter carries it on only a little. Within
 * REACHED of the target once past it, or when time runs out, it has arrived. A goal that takes twice as long as it
 * should, plus TIMEOUT_MARGIN_S, and is not within REACHED gives up.
 */
class GoalController {
  public:
    static constexpr float SLOW_DOWN_M = 0.05f;
    static constexpr float REACHED_M = 0.01f;
    static constexpr float SLOW_DOWN_RAD = 30.0f * static_cast<float>(M_PI) / 180.0f;
    static constexpr float REACHED_RAD = 3.0f * static_cast<float>(M_PI) / 180.0f;
    static constexpr float MIN_FRACTION = 0.2f;
    static constexpr float TIMEOUT_MARGIN_S = 2.0f;

    /** One target, finite, at a speed in (0, 1]. */
    static bool valid(const Goal &goal) {
        const float targets[3] = {goal.forward, goal.sideways, goal.turn};
        int set = 0;
        for (float target : targets) {
            if (!isFinite(target)) return false;
            set += target != 0;
        }
        return set == 1 && isFinite(goal.speed) && goal.speed > 0 && goal.speed <= 1;
    }

    /** Starts `goal` from here; `topSpeed` (m/s) and `topTurn` (rad/s) are what a full stick gives. */
    void start(const Goal &goal, float topSpeed, float topTurn) {
        target = goal;
        x = y = heading = elapsed = 0;
        const bool turning = goal.turn != 0;
        const float distance = std::fabs(turning ? goal.turn : goal.forward + goal.sideways);
        const float zone = turning ? SLOW_DOWN_RAD : SLOW_DOWN_M;
        const float pace = goal.speed * (turning ? topTurn : topSpeed);
        const float expected = (std::max(distance - zone, 0.0f) + std::min(distance, zone) / MIN_FRACTION) / pace;
        deadline = 2 * expected + TIMEOUT_MARGIN_S;
        current = GoalState::RUNNING;
    }

    /** One tick: the body's step and the time it took. Returns the sticks for the next tick, centred once it ends. */
    GoalSticks update(const BodyStep &step, float dt) {
        if (current != GoalState::RUNNING) return {};
        x += std::cos(heading) * step.forward - std::sin(heading) * step.left;
        y += std::sin(heading) * step.forward + std::cos(heading) * step.left;
        heading += step.turn;
        elapsed += dt;
        const float remaining = aim() - progress();
        const bool past = remaining * aim() <= 0, late = elapsed > deadline;
        if (std::fabs(remaining) <= (target.turn != 0 ? REACHED_RAD : REACHED_M) && (past || late)) {
            current = GoalState::REACHED;
            return {};
        }
        if (late) {
            current = GoalState::TIMED_OUT;
            return {};
        }
        const float zone = target.turn != 0 ? SLOW_DOWN_RAD : SLOW_DOWN_M;
        const float stick =
            std::copysign(target.speed * std::clamp(std::fabs(remaining) / zone, MIN_FRACTION, 1.0f), remaining);
        if (target.turn != 0) return {0, 0, stick};
        if (target.sideways != 0) return {0, stick, 0};
        return {stick, 0, 0};
    }

    void cancel() {
        if (current == GoalState::RUNNING) current = GoalState::CANCELLED;
    }

    GoalState state() const { return current; }
    bool running() const { return current == GoalState::RUNNING; }
    const Goal &goal() const { return target; }
    /** How far along the target's axis it has come: m, or rad for a turn. */
    float progress() const { return target.turn != 0 ? heading : target.sideways != 0 ? y : x; }
    /** How long it may take, in seconds. */
    float timeout() const { return deadline; }

  private:
    Goal target;
    GoalState current = GoalState::IDLE;
    float x = 0, y = 0, heading = 0; // in the frame the goal started in; the heading unwrapped
    float elapsed = 0, deadline = 0;

    float aim() const { return target.turn != 0 ? target.turn : target.sideways != 0 ? target.sideways : target.forward; }
};
