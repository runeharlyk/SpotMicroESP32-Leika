#pragma once

#include <kinematics.h>
#include <utils/math_utils.h>
#include <array>
#include <cmath>

/** Where the body has gone since the last reset, in the floor frame it faced then (REP-103: x forward, y left). */
struct OdometryEstimate {
    float x = 0, y = 0; // m
    float yaw = 0;      // rad, counter-clockwise, in [-pi, pi]
    float distance = 0; // m travelled along the path, never decreasing
};

/**
 * Dead reckoning from the commanded feet. A foot in stance is fixed to the floor, so in the body frame it moves by
 * the inverse of the body's motion: the rigid motion that best carries the stance feet from the last tick to this one
 * (a least-squares fit over the feet on the floor in both) is the body's motion, inverted.
 *
 * The heading integrates the same fit's yaw. The Pico's compass is uncalibrated and the gyro's yaw drifts with a bias
 * nothing estimates yet, while the feet's yaw has no drift and only the slip the translation has too; fusing the
 * gyro is a filter's job.
 *
 * It knows only what the gait commanded: servo lag, slip and short steps make the real robot travel less, so the
 * estimate needs calibrating against a tape measure.
 */
class KinematicOdometry {
  public:
    // A foot this far above the lowest one is in the air, whatever its phase: one a state eases down from mid-stride.
    static constexpr float LIFTED_M = 0.003f;

    /** Zeroes the estimate; the feet of the last tick still count, so the tick in progress is not lost. */
    void reset() { current = {}; }

    const OdometryEstimate &estimate() const { return current; }

    /** One control tick: the body the state left and which feet its gait has in stance. */
    void update(const body_state_t &body, const std::array<bool, 4> &stance) {
        float feet[4][2];
        bool contact[4];
        float lowest = body.feet[0][1];
        for (int leg = 1; leg < 4; leg++) lowest = std::min(lowest, body.feet[leg][1]);
        // The body stands at (xm, zm) in the feet's frame, turned by phi about the vertical; z points right.
        const float c = std::cos(body.phi * DEG2RAD_F), s = std::sin(body.phi * DEG2RAD_F);
        for (int leg = 0; leg < 4; leg++) {
            const float forward = body.feet[leg][0] - body.xm, left = body.zm - body.feet[leg][2];
            feet[leg][0] = c * forward + s * left;
            feet[leg][1] = -s * forward + c * left;
            contact[leg] = stance[leg] && body.feet[leg][1] - lowest < LIFTED_M;
        }
        if (primed) integrate(feet, contact);
        for (int leg = 0; leg < 4; leg++) {
            previous[leg][0] = feet[leg][0];
            previous[leg][1] = feet[leg][1];
            previousContact[leg] = contact[leg];
        }
        primed = true;
    }

  private:
    OdometryEstimate current;
    float previous[4][2] = {};
    bool previousContact[4] = {};
    bool primed = false;

    void integrate(const float feet[4][2], const bool contact[4]) {
        int count = 0;
        float before[2] = {0, 0}, after[2] = {0, 0};
        for (int leg = 0; leg < 4; leg++) {
            if (!contact[leg] || !previousContact[leg]) continue;
            count++;
            before[0] += previous[leg][0], before[1] += previous[leg][1];
            after[0] += feet[leg][0], after[1] += feet[leg][1];
        }
        if (count == 0) return;
        before[0] /= count, before[1] /= count, after[0] /= count, after[1] /= count;

        // The feet turn by -turn about their centroid; one foot alone shows no turn.
        float cross = 0, dot = 0;
        for (int leg = 0; leg < 4; leg++) {
            if (!contact[leg] || !previousContact[leg]) continue;
            const float px = previous[leg][0] - before[0], py = previous[leg][1] - before[1];
            const float qx = feet[leg][0] - after[0], qy = feet[leg][1] - after[1];
            cross += px * qy - py * qx;
            dot += px * qx + py * qy;
        }
        const float turn = count > 1 ? -std::atan2(cross, dot) : 0.0f;

        // A floor point p in the last body frame is R(-turn) (p - step) in this one; the centroids give the step.
        const float c = std::cos(turn), s = std::sin(turn);
        const float stepX = before[0] - (c * after[0] - s * after[1]);
        const float stepY = before[1] - (s * after[0] + c * after[1]);

        const float heading = current.yaw;
        current.x += std::cos(heading) * stepX - std::sin(heading) * stepY;
        current.y += std::sin(heading) * stepX + std::cos(heading) * stepY;
        current.yaw = std::remainder(heading + turn, 2.0f * static_cast<float>(M_PI));
        current.distance += std::hypot(stepX, stepY);
    }
};
