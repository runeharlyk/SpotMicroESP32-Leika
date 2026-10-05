#pragma once

#include <esp_log.h>
#include <kinematics.h>
#include <message_types.h>
#include <peripherals/imu/imu_math.h>
#include <utils/critical_damper.h>
#include <utils/math_utils.h>
#include <algorithm>
#include <cstring>

class MotionState {
  protected:
    virtual const char* name() const = 0;
    const KinConfig* kin = nullptr;
    body_state_t target_body_state;
    // A step in the body or gait targets settles to 95% in a third of a second, without overshoot.
    static constexpr float smoothing_omega = CriticalDamper::omegaFor(0.333f);
    float omega_offset = 0, psi_offset = 0;

    struct BodyDampers {
        CriticalDamper xm, ym, zm, phi, psi, omega;
    } body_dampers;

    // A state taking the legs over moves the feet from where the last one left them to its own, with a minimum-jerk
    // profile over this long: the walk can hand over a foot mid-stride, which used to snap down in one tick.
    static constexpr float FEET_EASE_S = 0.5f;
    float feet_from[4][4] = {};
    float feet_eased_s = 0;
    bool feet_captured = false;

    static void follow(CriticalDamper& damper, float& value, float target, float dt) {
        value = damper.step(value, target, dt, smoothing_omega);
    }

    void smoothToBody(body_state_t& body_state, float dt, const bool imuCompensate = false) {
        follow(body_dampers.xm, body_state.xm, target_body_state.xm, dt);
        follow(body_dampers.ym, body_state.ym, target_body_state.ym, dt);
        follow(body_dampers.zm, body_state.zm, target_body_state.zm, dt);
        follow(body_dampers.phi, body_state.phi, target_body_state.phi, dt);
        const float target_psi =
            clamp(target_body_state.psi - imuCompensate * psi_offset, -kin->max_pitch, kin->max_pitch);
        const float target_omega =
            clamp(target_body_state.omega - imuCompensate * omega_offset, -kin->max_roll, kin->max_roll);
        follow(body_dampers.psi, body_state.psi, target_psi, dt);
        follow(body_dampers.omega, body_state.omega, target_omega, dt);
    }

    void updateFeet(body_state_t& body_state) {
        if (std::memcmp(target_body_state.feet, body_state.feet, sizeof(body_state.feet)) != 0) {
            body_state.updateFeet(target_body_state.feet);
        }
    }

    // Starts and ends at rest: 10s^3 - 15s^4 + 6s^5 has no velocity or acceleration at either end.
    void easeFeet(body_state_t& body_state, float dt) {
        if (!feet_captured) {
            std::memcpy(feet_from, body_state.feet, sizeof(feet_from));
            feet_captured = true;
        }
        if (feet_eased_s >= FEET_EASE_S) return updateFeet(body_state);
        feet_eased_s = std::min(feet_eased_s + dt, FEET_EASE_S);
        const float s = feet_eased_s / FEET_EASE_S;
        const float blend = s * s * s * (10 - 15 * s + 6 * s * s);
        float feet[4][4];
        for (int foot = 0; foot < 4; foot++)
            for (int axis = 0; axis < 4; axis++)
                feet[foot][axis] =
                    feet_from[foot][axis] + (target_body_state.feet[foot][axis] - feet_from[foot][axis]) * blend;
        body_state.updateFeet(feet);
    }

  public:
    // Measured on the Pico: a positive omega lowers REP-103 roll and a positive psi lowers REP-103 pitch, so the
    // offsets are the IMU's angles with their signs reversed.
    void updateImuOffsets(const ImuSample& imu) {
        omega_offset = -RAD_TO_DEG_F(imu.rpy[0]);
        psi_offset = -RAD_TO_DEG_F(imu.rpy[1]);
    }
    virtual ~MotionState() {}

    // Before any other call: the variant's geometry, which stays for the state's life.
    virtual void configure(const KinConfig& config) {
        kin = &config;
        target_body_state.ym = config.default_body_height;
    }

    // A state taking over starts from the body at rest: the dampers' velocities are from when it last ran.
    virtual void resetSmoothing() {
        body_dampers = {};
        feet_captured = false;
        feet_eased_s = 0;
    }

    virtual void begin() { ESP_LOGI("Gait Planner", "Starting %s", name()); }

    virtual void end() { ESP_LOGI("Gait Planner", "Ending %s", name()); }

    virtual void handleCommand(const CommandMsg& cmd) {}

    virtual void step(body_state_t& body_state, float dt = 0.02f) {}
};