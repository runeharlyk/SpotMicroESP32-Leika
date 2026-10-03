#pragma once

#include <esp_log.h>
#include <kinematics.h>
#include <message_types.h>
#include <peripherals/imu/imu_math.h>
#include <utils/critical_damper.h>
#include <utils/math_utils.h>
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
    virtual void resetSmoothing() { body_dampers = {}; }

    virtual void begin() { ESP_LOGI("Gait Planner", "Starting %s", name()); }

    virtual void end() { ESP_LOGI("Gait Planner", "Ending %s", name()); }

    virtual void handleCommand(const CommandMsg& cmd) {}

    virtual void step(body_state_t& body_state, float dt = 0.02f) {}
};