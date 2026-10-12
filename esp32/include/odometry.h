#pragma once

#include <kinematics.h>
#include <peripherals/imu/imu_math.h>
#include <utils/math_utils.h>
#include <array>
#include <cmath>
#include <optional>

/** Where the body has gone since the last reset, in the floor frame it faced then (REP-103: x forward, y left). */
struct OdometryEstimate {
    float x = 0, y = 0; // m
    float yaw = 0;      // rad, counter-clockwise, in [-pi, pi]
    float distance = 0; // m travelled along the path, never decreasing
};

/** The body's motion over one tick, in the frame it faced at the start of the tick. */
struct BodyStep {
    float forward = 0, left = 0; // m
    float turn = 0;              // rad, counter-clockwise
};

/**
 * Dead reckoning from the commanded feet. A foot in stance is fixed to the floor, so in the body frame it moves by
 * the inverse of the body's motion: the rigid motion that best carries the stance feet from the last tick to this one
 * (a least-squares fit over the feet on the floor in both) is the body's motion, inverted.
 *
 * The heading integrates the same fit's yaw, which has no drift but misses what the feet do not command: FusedOdometry
 * corrects it with the gyro.
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
    /** Whether the last tick's turn was seen: two feet or more were down in it and the tick before. */
    bool turnSeen() const { return sawTurn; }

    /** One control tick: the body the state left and which feet its gait has in stance. Returns the step it took. */
    BodyStep update(const body_state_t &body, const std::array<bool, 4> &stance) {
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
        sawTurn = false;
        const BodyStep step = primed ? integrate(feet, contact) : BodyStep {};
        for (int leg = 0; leg < 4; leg++) {
            previous[leg][0] = feet[leg][0];
            previous[leg][1] = feet[leg][1];
            previousContact[leg] = contact[leg];
        }
        primed = true;
        return step;
    }

  private:
    OdometryEstimate current;
    float previous[4][2] = {};
    bool previousContact[4] = {};
    bool primed = false;
    bool sawTurn = false;

    BodyStep integrate(const float feet[4][2], const bool contact[4]) {
        int count = 0;
        float before[2] = {0, 0}, after[2] = {0, 0};
        for (int leg = 0; leg < 4; leg++) {
            if (!contact[leg] || !previousContact[leg]) continue;
            count++;
            before[0] += previous[leg][0], before[1] += previous[leg][1];
            after[0] += feet[leg][0], after[1] += feet[leg][1];
        }
        if (count == 0) return {};
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
        sawTurn = count > 1;
        const float turn = sawTurn ? -std::atan2(cross, dot) : 0.0f;

        // A floor point p in the last body frame is R(-turn) (p - step) in this one; the centroids give the step.
        const float c = std::cos(turn), s = std::sin(turn);
        const float stepX = before[0] - (c * after[0] - s * after[1]);
        const float stepY = before[1] - (s * after[0] + c * after[1]);

        const float heading = current.yaw;
        current.x += std::cos(heading) * stepX - std::sin(heading) * stepY;
        current.y += std::sin(heading) * stepX + std::cos(heading) * stepY;
        current.yaw = std::remainder(heading + turn, 2.0f * static_cast<float>(M_PI));
        current.distance += std::hypot(stepX, stepY);
        return {stepX, stepY, turn};
    }
};

/** The fused estimate, and the heading the feet alone give: against the fused one over a turn, the gait's scale. */
struct FusedEstimate : OdometryEstimate {
    float kinematicYaw = 0; // rad, as yaw
};

/**
 * The kinematic odometry with its heading corrected by the gyro, fed once per control tick.
 *
 * The feet and the gyro each measure the yaw rate. The gyro's z rate has only white noise and a residual bias left by
 * the boot calibration; the feet's has no bias, but misses what the gait does not command: the trot rocks the body in
 * yaw, and slip makes a turn come up short (by 9 % in MuJoCo). While the feet keep still, a scalar Kalman filter
 * estimates the bias from the difference of the two rates (a zero angular rate update); the heading integrates the two
 * rates weighted by their variances, the gyro's while walking and mostly the feet's while standing.
 * The position integrates the feet's displacement in the fused heading.
 *
 * The compass stays out (uncalibrated), and so does the accelerometer: at a walk the body barely accelerates on
 * average, and a degree of tilt error alone reads as 0.17 m/s^2, so a stall would not show above the noise.
 *
 * Without a gyro rate it is the kinematic odometry, and the bias stays where it was.
 */
class FusedOdometry {
  public:
    // The gyro's z noise on a still Pico, measured 2026-10-01.
    static constexpr float GYRO_NOISE = 0.0012f; // rad/s
    // The bias the filter starts out expecting: domain_rand.py's per-episode gyro bias, the scale of one a boot
    // calibration leaves when the robot moved through it.
    static constexpr float BIAS_PRIOR = 0.05f; // rad/s
    // How fast the bias wanders, assumed: about 0.006 rad/s over an hour, a few degrees' warming.
    static constexpr float BIAS_WALK = 1e-4f; // rad/s per square-root second
    // The feet's yaw rate error, at a standstill (planted feet barely turn: assumed), per m/s walked and per rad/s
    // turned. In MuJoCo, against its gyro, it is 0.08 rad/s per tick at 0.04 m/s and 0.17 rad/s per tick at 0.46 rad/s.
    static constexpr float KIN_STILL = 0.0002f;  // rad/s
    static constexpr float KIN_PER_SPEED = 2.0f; // rad/s per m/s
    static constexpr float KIN_PER_TURN = 0.4f;  // rad/s per rad/s
    // The feet's motion counts on, fading with this time constant, once they stop: the body coasts on (in MuJoCo from
    // 0.74 rad/s to 0.04 in 70 ms after a turn) and the robot's servos lag the commanded feet.
    static constexpr float SETTLE_S = 0.2f;
    // Feet whose motion is worth less than this keep the robot still: then, and only then, the gyro shows its bias.
    static constexpr float STILL_RATE = 0.01f; // rad/s
    // A sample older than this, or a tick longer, counts as no gyro.
    static constexpr int64_t GYRO_STALE_US = 50000;
    static constexpr float MAX_DT_S = 0.1f;

    /** The yaw rate about the body's up axis, when the sample has a gyro reading that is fresh at `nowUs`. */
    static std::optional<float> yawRate(const ImuSample &imu, int64_t nowUs) {
        if (!(imu.valid & ImuValid::GYRO) || nowUs - imu.t_us > GYRO_STALE_US) return std::nullopt;
        return imu.gyro[2];
    }

    /** Zeroes the estimate; the bias, a property of the gyro, stays. */
    void reset() {
        kinematic.reset();
        current = {};
    }

    FusedEstimate estimate() const {
        FusedEstimate e;
        static_cast<OdometryEstimate &>(e) = current;
        e.kinematicYaw = kinematic.estimate().yaw;
        return e;
    }
    float gyroBias() const { return bias; }

    /**
     * One control tick of `dt` seconds: the body and stance as KinematicOdometry takes them, and the gyro's yaw rate.
     * Returns the step it took.
     */
    BodyStep update(const body_state_t &body, const std::array<bool, 4> &stance, std::optional<float> gyroRate,
                    float dt) {
        const BodyStep step = kinematic.update(body, stance);
        float turn = step.turn;
        if (gyroRate && dt > 0 && dt <= MAX_DT_S) {
            biasVariance += BIAS_WALK * BIAS_WALK * dt;
            // Feet that did not see the turn leave it to the gyro.
            turn = kinematic.turnSeen() ? fuse(*gyroRate, step, dt) : (*gyroRate - bias) * dt;
        }
        const float c = std::cos(current.yaw), s = std::sin(current.yaw);
        current.x += c * step.forward - s * step.left;
        current.y += s * step.forward + c * step.left;
        current.yaw = std::remainder(current.yaw + turn, 2.0f * static_cast<float>(M_PI));
        current.distance += std::hypot(step.forward, step.left);
        return {step.forward, step.left, turn};
    }

  private:
    KinematicOdometry kinematic;
    OdometryEstimate current;
    float bias = 0;
    float biasVariance = BIAS_PRIOR * BIAS_PRIOR;
    float motionVariance = 0; // of the feet's rate, from their motion

    static float square(float value) { return value * value; }

    // The gyro reads the feet's rate plus the bias, give or take both their noises. On feet that keep still, their
    // difference updates the bias; on moving ones it would also carry the feet's undercount, which is no noise. The
    // two rates, weighted by their variances, give the turn.
    float fuse(float gyroRate, const BodyStep &step, float dt) {
        const float feetRate = step.turn / dt, speed = std::hypot(step.forward, step.left) / dt;
        motionVariance = std::max(square(KIN_PER_SPEED * speed) + square(KIN_PER_TURN * feetRate),
                                  motionVariance * std::exp(-dt / SETTLE_S));
        const float feetVariance = square(KIN_STILL) + motionVariance;
        if (motionVariance < square(STILL_RATE)) {
            const float gain = biasVariance / (biasVariance + square(GYRO_NOISE) + feetVariance);
            bias += gain * (gyroRate - feetRate - bias);
            biasVariance *= 1 - gain;
        }
        const float gyroVariance = square(GYRO_NOISE) + biasVariance;
        const float gyroWeight = feetVariance / (feetVariance + gyroVariance);
        return (gyroWeight * (gyroRate - bias) + (1 - gyroWeight) * feetRate) * dt;
    }
};
