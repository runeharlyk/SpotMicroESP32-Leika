#pragma once

#include <esp_log.h>
#include <kinematics.h>
#include <message_types.h>
#include <peripherals/imu/imu_math.h>
#include <utils/math_utils.h>
#include <cstring>

class MotionState {
  protected:
    virtual const char* name() const = 0;
    static constexpr const float (&default_feet_pos)[4][4] = KinConfig::default_feet_positions;
    body_state_t target_body_state;
    static constexpr float default_smoothing_factor = 0.03f;
    float omega_offset = 0, psi_offset = 0;

    void lerpToBody(body_state_t& body_state, const bool imuCompensate = false,
                    const float smoothing_factor = default_smoothing_factor) {
        body_state.xm = lerp(body_state.xm, target_body_state.xm, smoothing_factor);
        body_state.ym = lerp(body_state.ym, target_body_state.ym, smoothing_factor);
        body_state.zm = lerp(body_state.zm, target_body_state.zm, smoothing_factor);
        body_state.phi = lerp(body_state.phi, target_body_state.phi, smoothing_factor);
        const float target_psi =
            clamp(target_body_state.psi - imuCompensate * psi_offset, -KinConfig::max_pitch, KinConfig::max_pitch);
        const float target_omega =
            clamp(target_body_state.omega - imuCompensate * omega_offset, -KinConfig::max_roll, KinConfig::max_roll);
        body_state.psi = lerp(body_state.psi, target_psi, smoothing_factor);
        body_state.omega = lerp(body_state.omega, target_omega, smoothing_factor);
    }

    void updateFeet(body_state_t& body_state, const float smoothing_factor = default_smoothing_factor) {
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

    virtual void begin() { ESP_LOGI("Gait Planner", "Starting %s", name()); }

    virtual void end() { ESP_LOGI("Gait Planner", "Ending %s", name()); }

    virtual void handleCommand(const CommandMsg& cmd) {}

    virtual void step(body_state_t& body_state, float dt = 0.02f) {}
};