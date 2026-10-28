#pragma once

#include <odometry.h>
#include <utils/critical_damper.h>
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
 * Drives one goal to its target on the body's own motion, fed one step per control tick, in the frame the goal started
 * in. It judges the pose the robot would come to rest in were its sticks centred now: where the body is, carried on by
 * the step the walk's stop would still take (WalkState::stopStep).
 *
 * The remaining distance along the target's axis, or the remaining turn, sets the stick: the asked speed, slowed in
 * proportion over the slowdown zone, down to MIN_FRACTION of it. The zone is SLOW_DOWN, or the distance the asked
 * speed covers in two stops' coasts when that is longer, so a fast walk has room to slow. Once the resting pose reaches
 * the target it centres the sticks and waits for the walk to have stood still for STILL_S, since the stop carries the
 * body on by an amount that also depends on the gait's phase, and judges where the robot stands: within REACHED of the
 * target, and a straight goal within CROSS_REACHED_M of its start line, it has REACHED. Otherwise a corrective pass
 * walks the remainder, up to MAX_PASSES passes in all, and then it has TIMED_OUT. So has a pass that takes longer than
 * twice its expected time plus TIMEOUT_MARGIN_S.
 *
 * A straight goal holds the heading it started on with the turn stick and its start line with the stick across it;
 * one that rests on the target but off the line gets a pass that walks only across.
 */
class GoalController {
  public:
    static constexpr float SLOW_DOWN_M = 0.05f;
    static constexpr float REACHED_M = 0.01f;
    static constexpr float SLOW_DOWN_RAD = 30.0f * static_cast<float>(M_PI) / 180.0f;
    static constexpr float REACHED_RAD = 3.0f * static_cast<float>(M_PI) / 180.0f;
    static constexpr float MIN_FRACTION = 0.2f;
    static constexpr int MAX_PASSES = 3;
    static constexpr float HEADING_GAIN = 2.0f; // turn stick per rad off the start heading
    static constexpr float HEADING_STICK_MAX = 0.3f;
    static constexpr float CROSS_GAIN = 10.0f; // stick across the line per m off it
    static constexpr float CROSS_STICK_MAX = 0.3f;
    static constexpr float CROSS_REACHED_M = 0.02f;
    static constexpr float TIMEOUT_MARGIN_S = 2.0f;
    // How long the walk stands still before the pose is judged: the body coasts on once the feet stop (in MuJoCo from
    // 0.74 rad/s to 0.04 in 70 ms after a turn) and the servos lag the commanded feet, as FusedOdometry::SETTLE_S has.
    static constexpr float STILL_S = 0.2f;
    // How far the walk carries on once its sticks centre, as a time at the speed it walked: its stroke follows the
    // sticks through a critical damper that settles in a third of a second (MotionState::smoothingOmega()), whose
    // integral from a steady stroke is 2 / omega.
    static constexpr float STOP_S = 2 / CriticalDamper::omegaFor(0.333f);

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
        topSpeedMs = topSpeed;
        topTurnRads = topTurn;
        x = y = heading = 0;
        rest = {};
        spent = elapsed = still = 0;
        passCount = 0;
        current = GoalState::RUNNING;
        beginPass();
    }

    /**
     * One tick: the body's step and the time it took, the step the walk's stop would still take from here and whether
     * the walk stands still. Returns the sticks for the next tick, centred while it waits for the walk to stand and
     * once it ends.
     */
    GoalSticks update(const BodyStep &step, const BodyStep &toRest, bool atRest, float dt) {
        if (current != GoalState::RUNNING) return {};
        x += std::cos(heading) * step.forward - std::sin(heading) * step.left;
        y += std::sin(heading) * step.forward + std::cos(heading) * step.left;
        heading += step.turn;
        rest = {x + std::cos(heading) * toRest.forward - std::sin(heading) * toRest.left,
                y + std::sin(heading) * toRest.forward + std::cos(heading) * toRest.left, heading + toRest.turn};
        elapsed += dt;
        still = atRest ? still + dt : 0;
        if (elapsed > deadline) {
            current = GoalState::TIMED_OUT;
            return {};
        }
        if (settling) {
            if (still < STILL_S) return {};
            if (within()) {
                current = GoalState::REACHED;
                return {};
            }
            if (passCount == MAX_PASSES) {
                current = GoalState::TIMED_OUT;
                return {};
            }
            beginPass();
        }
        if (arriving() && std::fabs(cross()) <= CROSS_REACHED_M) {
            settling = true;
            return {};
        }
        return sticks();
    }

    void cancel() {
        if (current == GoalState::RUNNING) current = GoalState::CANCELLED;
    }

    GoalState state() const { return current; }
    bool running() const { return current == GoalState::RUNNING; }
    const Goal &goal() const { return target; }
    /** How far along the target's axis it has come: m, or rad for a turn. */
    float progress() const { return target.turn != 0 ? heading : target.sideways != 0 ? y : x; }
    /** How long it may take, in seconds: the passes so far and the longest the one under way may take. */
    float timeout() const { return spent + deadline; }
    /** The passes started, the first included. */
    int passes() const { return passCount; }

  private:
    struct Pose {
        float x = 0, y = 0, heading = 0;
    };

    Goal target;
    GoalState current = GoalState::IDLE;
    float topSpeedMs = 0, topTurnRads = 0;
    float x = 0, y = 0, heading = 0; // in the frame the goal started in; the heading unwrapped
    Pose rest;                       // where the robot would come to rest, in the same frame
    int passCount = 0;
    // The pass under way: the sign of the remainder it walks (0 for a pass only across the line), whether it waits for
    // the walk to stand, its clock and its time limit, and the time the passes before took.
    float direction = 0;
    bool settling = false;
    float elapsed = 0, deadline = 0, spent = 0;
    float still = 0; // how long the walk has stood still

    bool linear() const { return target.turn == 0; }
    float aim() const {
        return target.turn != 0 ? target.turn : target.sideways != 0 ? target.sideways : target.forward;
    }
    // Of the target, signed as it, from where the robot would rest.
    float remaining() const {
        return aim() - (target.turn != 0 ? rest.heading : target.sideways != 0 ? rest.y : rest.x);
    }
    // How far the resting pose lies off the start line: left of it for a forward goal, ahead of it for a sideways one.
    float cross() const { return target.turn != 0 ? 0 : target.sideways != 0 ? rest.x : rest.y; }
    float pace() const { return target.speed * (linear() ? topSpeedMs : topTurnRads); }
    float zone() const { return std::max(linear() ? SLOW_DOWN_M : SLOW_DOWN_RAD, 2 * pace() * STOP_S); }
    // The share of the asked speed the remainder drives at.
    float share() const { return std::clamp(std::fabs(remaining()) / zone(), MIN_FRACTION, 1.0f); }
    // The resting pose has reached the target; a pass only across the line has no axis to walk.
    bool arriving() const { return remaining() * direction <= 0; }

    bool within() const {
        if (!linear()) return std::fabs(remaining()) <= REACHED_RAD;
        return std::fabs(remaining()) <= REACHED_M && std::fabs(cross()) <= CROSS_REACHED_M;
    }

    // The first pass walks its target however short; a later one whose axis is within the tolerance walks only across.
    void beginPass() {
        passCount++;
        const float left = remaining();
        direction = passCount > 1 && linear() && std::fabs(left) <= REACHED_M ? 0 : std::copysign(1.0f, left);
        settling = false;
        spent += elapsed;
        elapsed = 0;
        const float distance = std::fabs(left);
        const float expected = (std::max(distance - zone(), 0.0f) + std::min(distance, zone()) / MIN_FRACTION) / pace();
        deadline = 2 * expected + TIMEOUT_MARGIN_S;
    }

    GoalSticks sticks() const {
        const float drive = arriving() ? 0 : std::copysign(target.speed * share(), remaining());
        if (target.turn != 0) return {0, 0, drive};
        // Off the line walks back onto it, and off the heading turns back to it.
        const float across = std::clamp(CROSS_GAIN * cross(), -CROSS_STICK_MAX, CROSS_STICK_MAX);
        const float hold = std::clamp(-HEADING_GAIN * heading, -HEADING_STICK_MAX, HEADING_STICK_MAX);
        if (target.sideways != 0) return {-across, drive, hold};
        return {drive, -across, hold};
    }
};
