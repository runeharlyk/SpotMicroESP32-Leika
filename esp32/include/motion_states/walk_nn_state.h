#pragma once

// Only built with USE_POLICY (see features.h), which guarantees the Mini geometry, the MPU6050 and
// an exported policy header.

#include <cmath>
#include <esp_log.h>
#include <motion_states/state.h>
#include <policy/leika_policy.h>
#include <spot_pico/kinematics.h>
#include <spot_pico/residual_gait.h>

// WALK_NN: the residual policy from simulation/export_policy.py on top of the spot_pico baseline gait.
// Every tick mirrors QuadrupedMjEnv.step (control_mode residual_pure) in
// simulation/src/envs/quadruped_mj_env.py; keep the two in lockstep.
//
// On entry the body first settles into the stance the policy was trained from (the env's reset pose),
// with the regular smoothing; only then does the policy engage, from phase 0 with a zero action history.
class WalkNNState : public MotionState {
  public:
    // IMU mounting: v_sensor = IMU_FROM_BASE * v_base, with the base frame +X left, +Y rear, +Z up.
    // Assumed: sensor flat, x forward, y left, z up. BENCH TEST before walking (raise the log level to
    // see the observation): level -> gravity (0, 0, -1); nose down -> pitch > 0; left side down ->
    // roll < 0; turning left (CCW from above) -> gyro z > 0. Fix this matrix if any sign is off.
    static constexpr float IMU_FROM_BASE[3][3] = {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}};

    const char *name() const override { return "WalkNN"; }

    // Boot-time check that the baked network reproduces its golden output.
    static bool verifyPolicy() {
        const float err = leika_policy::selfCheck();
        ESP_LOGI("WalkNN", "policy self-check max error %.3e (%s)", err, err < 1e-4f ? "OK" : "FAIL");
        return err < 1e-4f;
    }

    void begin() override {
        MotionState::begin();
        engaged_ = false;
        target_body_state.xm = target_body_state.zm = 0;
        target_body_state.omega = target_body_state.phi = target_body_state.psi = 0;
        target_body_state.ym = STANCE_BODY_HEIGHT;
        target_body_state.updateFeet(KinConfig::default_feet_positions);
    }

    // Joystick [-1, 1] -> the command ranges the policy was trained on (backward range is smaller).
    // Sign conventions: stick right (+lx) is a rightward, i.e. negative-left, velocity, and stick
    // right (+rx) turns clockwise, i.e. a negative CCW yaw rate. Verify the turn direction on hardware.
    void handleCommand(const CommandMsg &c) override {
        namespace lp = leika_policy;
        command_[0] = c.ly * (c.ly >= 0.f ? lp::CMD_VX_MAX : -lp::CMD_VX_MIN);
        command_[1] = -c.lx * lp::CMD_VY_MAX;
        command_[2] = -c.rx * lp::CMD_YAW_MAX;
    }

    // Latest IMU reading and the joint angles (rad, sim leg order) the kinematics produced last tick.
    void observe(const float quat[4], const float gyro[3], const float joint_rad[12]) {
        for (int i = 0; i < 4; ++i) quat_[i] = quat[i];
        for (int i = 0; i < 3; ++i) gyro_[i] = gyro[i];
        for (int i = 0; i < 12; ++i) joint_now_[i] = joint_rad[i];
    }

    // The policy commands servo targets directly (the sim has no command smoothing).
    bool engaged() const { return engaged_; }

    void step(body_state_t &body_state, float dt = 0.02f) override {
        if (!engaged_) {
            settle(body_state);
            return;
        }
        if (dt > 2.f * leika_policy::CONTROL_DT) {
            ESP_LOGW("WalkNN", "control tick took %.1f ms; the policy assumes %.0f ms", dt * 1e3f,
                     leika_policy::CONTROL_DT * 1e3f);
        }
        runPolicy(body_state);
    }

  private:
    // Body height that puts the firmware's default feet exactly on the trained spot_pico stance.
    static constexpr float STANCE_BODY_HEIGHT = -spot_pico::stanceFoot(0, 2);
    static constexpr float SETTLE_TOLERANCE = 5e-4f;  // m, and deg for the body angles

    bool engaged_ = false;
    float command_[3] = {0, 0, 0};
    float quat_[4] = {1, 0, 0, 0};
    float gyro_[3] = {0, 0, 0};
    // QuadrupedMjEnv builds its observation before updating prev_action / prev_joint_cmd, so the
    // observation for action t+1 holds the joint command and action of step t-1, next to phase t.
    // At tick t: joint_now_ = q_t (observed), joint_prev_ = q_{t-1}, action_now_ = a_t, action_prev_ = a_{t-1}.
    float joint_now_[12] = {0};
    float joint_prev_[12] = {0};
    float action_now_[leika_policy::ACT_DIM] = {0};
    float action_prev_[leika_policy::ACT_DIM] = {0};
    float phase_ = 0;
    float yaw_origin_ = 0;

    void settle(body_state_t &body_state) {
        lerpToBody(body_state);
        float worst = std::fabs(body_state.ym - target_body_state.ym);
        worst = std::fmax(worst, std::fabs(body_state.xm) + std::fabs(body_state.zm));
        worst = std::fmax(worst, std::fabs(body_state.omega) + std::fabs(body_state.phi) + std::fabs(body_state.psi));
        for (int i = 0; i < 4; ++i) {
            for (int k = 0; k < 3; ++k) {
                body_state.feet[i][k] = lerp(body_state.feet[i][k], target_body_state.feet[i][k], default_smoothing_factor);
                worst = std::fmax(worst, std::fabs(body_state.feet[i][k] - target_body_state.feet[i][k]));
            }
        }
        if (worst < SETTLE_TOLERANCE) engage(body_state);
    }

    // The env's reset: stand pose as the previous joint command, phase 0, zero action history, yaw
    // measured from here. joint_now_ receives the same stand pose through observe() on the next tick.
    void engage(body_state_t &body_state) {
        body_state = target_body_state;
        for (int leg = 0; leg < spot_pico::LEG_COUNT; ++leg) {
            float foot[3];
            spot_pico::defaultFoot(leg, foot);
            spot_pico::legIK(leg, foot, joint_prev_ + leg * 3);
        }
        for (int i = 0; i < leika_policy::ACT_DIM; ++i) action_now_[i] = action_prev_[i] = 0;
        phase_ = 0;
        float R[3][3];
        baseRotation(R);
        yaw_origin_ = std::atan2(R[1][0], R[0][0]);
        engaged_ = true;
        ESP_LOGI("WalkNN", "stance reached, policy engaged");
    }

    void runPolicy(body_state_t &body_state) {
        namespace lp = leika_policy;
        float obs[lp::OBS_DIM];
        buildObservation(obs);
        for (int i = 0; i < 12; ++i) joint_prev_[i] = joint_now_[i];
        for (int i = 0; i < lp::ACT_DIM; ++i) action_prev_[i] = action_now_[i];
        lp::infer(obs, action_now_);

        spot_pico::GaitParams gait;
        spot_pico::analyticGait(command_, lp::GAIT_COEF, gait);
        phase_ = spot_pico::advancePhase(phase_, gait, lp::CONTROL_DT);
        float feet[spot_pico::LEG_COUNT][3];
        spot_pico::generateFeet(gait, phase_, feet);

        // spot_pico base frame -> firmware world frame (level body at STANCE_BODY_HEIGHT).
        body_state.xm = body_state.zm = 0;
        body_state.omega = body_state.phi = body_state.psi = 0;
        body_state.ym = STANCE_BODY_HEIGHT;
        for (int fw = 0; fw < spot_pico::LEG_COUNT; ++fw) {
            const int leg = spot_pico::FIRMWARE_TO_SIM_LEG[fw];
            float foot[3];
            for (int k = 0; k < 3; ++k) foot[k] = feet[leg][k] + action_now_[leg * 3 + k] * lp::FOOT_RESIDUAL;
            body_state.feet[fw][0] = -foot[1];
            body_state.feet[fw][1] = foot[2] + STANCE_BODY_HEIGHT;
            body_state.feet[fw][2] = foot[0];
            body_state.feet[fw][3] = 1;
        }
    }

    // World-from-base rotation: DMP quaternion (world-from-sensor) composed with the mounting.
    void baseRotation(float R[3][3]) const {
        const float w = quat_[0], x = quat_[1], y = quat_[2], z = quat_[3];
        const float S[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                               {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                               {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                R[r][c] = S[r][0] * IMU_FROM_BASE[0][c] + S[r][1] * IMU_FROM_BASE[1][c] + S[r][2] * IMU_FROM_BASE[2][c];
            }
        }
    }

    void buildObservation(float *obs) const {
        namespace lp = leika_policy;
        float R[3][3];
        baseRotation(R);
        int o = 0;
        for (int i = 0; i < 3; ++i) obs[o++] = -R[2][i];  // gravity (world -Z) in the base frame
        for (int i = 0; i < 3; ++i) {
            obs[o++] = IMU_FROM_BASE[0][i] * gyro_[0] + IMU_FROM_BASE[1][i] * gyro_[1] + IMU_FROM_BASE[2][i] * gyro_[2];
        }
        obs[o++] = std::atan2(R[2][1], R[2][2]);
        obs[o++] = std::asin(std::fmax(-1.f, std::fmin(1.f, -R[2][0])));
        obs[o++] = spot_pico::linkage::wrap(std::atan2(R[1][0], R[0][0]) - yaw_origin_);
        for (int i = 0; i < 12; ++i) obs[o++] = joint_prev_[i];
        obs[o++] = std::sin(2.f * (float)M_PI * phase_);
        obs[o++] = std::cos(2.f * (float)M_PI * phase_);
        for (int i = 0; i < 3; ++i) obs[o++] = command_[i];
        for (int i = 0; i < lp::ACT_DIM; ++i) obs[o++] = action_prev_[i];
        static_assert(3 + 3 + 3 + 12 + 2 + 3 + lp::ACT_DIM == lp::OBS_DIM, "observation layout drifted");
        ESP_LOGD("WalkNN", "obs grav[%.2f %.2f %.2f] gyro[%.2f %.2f %.2f] rpy[%.2f %.2f %.2f]", obs[0], obs[1],
                 obs[2], obs[3], obs[4], obs[5], obs[6], obs[7], obs[8]);
    }
};
