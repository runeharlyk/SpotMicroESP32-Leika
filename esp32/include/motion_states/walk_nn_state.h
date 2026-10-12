#pragma once

// WALK_NN exists only in a firmware built with a policy: simulation/export_policy.py writes the header.
#if __has_include(<policy/leika_policy.h>)
#define WALK_NN_POLICY 1

#include <motion_states/state.h>
#include <policy/leika_policy.h>
#include <spot_pico/joint_map.h>
#include <spot_pico/kinematics.h>
#include <spot_pico/residual_gait.h>
#include <cmath>
#include <cstring>

/**
 * WALK_NN: the residual policy on top of the spot_pico baseline gait, for the Mini only. Every engaged tick mirrors
 * QuadrupedMjEnv.step (control_mode residual_pure) in simulation/src/envs/quadruped_mj_env.py; keep the two in
 * lockstep. The state drives the joints itself, in the spot_pico geometry the policy was trained on, and hands their
 * angles to the firmware through spot_pico/joint_map.h.
 *
 * On entry the joints ease from their last commands to the trained stance; only then does the policy engage, with the
 * env's reset: phase 0, zero action history, the stand pose as the previous joint command and yaw measured from there.
 */
class WalkNNState : public MotionState {
  public:
    static constexpr float SETTLE_S = 1.0f;

    /** Whether the baked network reproduces its golden output; checked once at boot. */
    static bool verifyPolicy() {
        const float err = leika_policy::selfCheck();
        ESP_LOGI("WalkNN", "policy self-check max error %.3e (%s)", err, err < 1e-4f ? "OK" : "FAIL");
        return err < 1e-4f;
    }

    void begin() override {
        MotionState::begin();
        engaged_ = false;
        from_captured_ = false;
        settled_s_ = 0;
        command_[0] = command_[1] = command_[2] = 0;
        writeBody(STANCE_FEET);
    }

    // Joystick [-1, 1] to the command ranges the policy was trained on, whose backward range is smaller. Stick right
    // (+lx) is a negative leftward velocity and turns clockwise (+rx), a negative CCW yaw rate.
    void handleCommand(const CommandMsg &c) override {
        namespace lp = leika_policy;
        command_[0] = c.ly * (c.ly >= 0.f ? lp::CMD_VX_MAX : -lp::CMD_VX_MIN);
        command_[1] = -c.lx * lp::CMD_VY_MAX;
        command_[2] = -c.rx * lp::CMD_YAW_MAX;
    }

    void observe(const ImuSample &imu) { imu_ = imu; }

    bool engaged() const { return engaged_; }

    void step(body_state_t &body_state, float dt = 0.02f) override {
        if (!engaged_) {
            if (settled_s_ >= SETTLE_S) engage();
            settled_s_ = std::fmin(settled_s_ + dt, SETTLE_S);
        }
        if (engaged_) runPolicy();
        body_state = body_;
    }

    bool jointAngles(float degrees[12]) override {
        if (engaged_) {
            spot_pico::firmwareAngles(joints_now_, degrees);
            return true;
        }
        if (!from_captured_) {
            std::memcpy(from_, degrees, sizeof(from_));
            from_captured_ = true;
        }
        const float s = settled_s_ / SETTLE_S;
        const float blend = s * s * s * (10 - 15 * s + 6 * s * s);
        for (int i = 0; i < 12; i++) degrees[i] = from_[i] + (stanceAngles()[i] - from_[i]) * blend;
        return true;
    }

    /**
     * The policy's observation from the latest IMU sample and the state's history (OBS_DIM floats, in the spot_pico
     * base frame: +X left, +Y rear, +Z up), computed as QuadrupedMjEnv._get_obs computes it from MuJoCo's base.
     */
    void observation(float *obs) const {
        namespace lp = leika_policy;
        float R[3][3];
        worldFromBase(R);
        int o = 0;
        for (int i = 0; i < 3; ++i) obs[o++] = -R[2][i];
        const Vec3 &g = imu_.gyro;
        obs[o++] = g[1];
        obs[o++] = -g[0];
        obs[o++] = g[2];
        obs[o++] = std::atan2(R[2][1], R[2][2]);
        obs[o++] = std::asin(std::fmax(-1.f, std::fmin(1.f, -R[2][0])));
        obs[o++] = spot_pico::wrap(yawOf(R) - yaw_origin_);
        for (int i = 0; i < 12; ++i) obs[o++] = joints_before_last_[i];
        obs[o++] = std::sin(2.f * (float)M_PI * phase_);
        obs[o++] = std::cos(2.f * (float)M_PI * phase_);
        for (int i = 0; i < 3; ++i) obs[o++] = command_[i];
        for (int i = 0; i < lp::ACT_DIM; ++i) obs[o++] = action_before_last_[i];
        static_assert(3 + 3 + 3 + 12 + 2 + 3 + lp::ACT_DIM == lp::OBS_DIM, "observation layout drifted");
    }

  protected:
    const char *name() const override { return "WalkNN"; }

  private:
    struct Feet {
        float at[spot_pico::LEG_COUNT][3];
    };

    static Feet stanceFeet() {
        Feet feet;
        for (int leg = 0; leg < spot_pico::LEG_COUNT; ++leg) spot_pico::defaultFoot(leg, feet.at[leg]);
        return feet;
    }

    static void legAngles(const Feet &feet, float q[12]) {
        for (int leg = 0; leg < spot_pico::LEG_COUNT; ++leg) spot_pico::legIK(leg, feet.at[leg], q + leg * 3);
    }

    inline static const Feet STANCE_FEET = stanceFeet();

    static const float *stanceAngles() {
        static const auto angles = [] {
            struct Table {
                float q[12], degrees[12];
            } t {};
            legAngles(STANCE_FEET, t.q);
            spot_pico::firmwareAngles(t.q, t.degrees);
            return t;
        }();
        return angles.degrees;
    }

    bool engaged_ = false;
    bool from_captured_ = false;
    float settled_s_ = 0;
    float from_[12] = {};
    float command_[3] = {0, 0, 0};
    ImuSample imu_;
    body_state_t body_;

    // The env builds its observation before it records the step's joint command and action, so the observation for
    // tick t holds those of tick t - 2 next to the phase of tick t.
    float joints_now_[12] = {};
    float joints_last_[12] = {};
    float joints_before_last_[12] = {};
    float action_last_[leika_policy::ACT_DIM] = {};
    float action_before_last_[leika_policy::ACT_DIM] = {};
    float phase_ = 0;
    float yaw_origin_ = 0;

    void engage() {
        legAngles(STANCE_FEET, joints_last_);
        std::memcpy(joints_before_last_, joints_last_, sizeof(joints_last_));
        std::memset(action_last_, 0, sizeof(action_last_));
        std::memset(action_before_last_, 0, sizeof(action_before_last_));
        phase_ = 0;
        float R[3][3];
        worldFromBase(R);
        yaw_origin_ = yawOf(R);
        engaged_ = true;
        ESP_LOGI("WalkNN", "stance reached, policy engaged");
    }

    void runPolicy() {
        namespace lp = leika_policy;
        float obs[lp::OBS_DIM];
        observation(obs);
        float action[lp::ACT_DIM];
        lp::infer(obs, action);

        spot_pico::GaitParams gait;
        spot_pico::analyticGait(command_, lp::GAIT_COEF, gait);
        phase_ = spot_pico::advancePhase(phase_, gait, lp::CONTROL_DT);
        Feet feet;
        spot_pico::generateFeet(gait, phase_, feet.at);
        for (int leg = 0; leg < spot_pico::LEG_COUNT; ++leg)
            for (int k = 0; k < 3; ++k) feet.at[leg][k] += action[leg * 3 + k] * lp::FOOT_RESIDUAL;

        std::memcpy(joints_before_last_, joints_last_, sizeof(joints_last_));
        std::memcpy(action_before_last_, action_last_, sizeof(action_last_));
        legAngles(feet, joints_now_);
        std::memcpy(joints_last_, joints_now_, sizeof(joints_now_));
        std::memcpy(action_last_, action, sizeof(action));
        writeBody(feet);
    }

    // The body the classic modes take over from: level at the stance depth, each firmware foot displaced from its
    // default as the spot_pico foot is from its stance (spot_pico +X left, +Y rear, +Z up to x forward, y up, z left).
    void writeBody(const Feet &feet) {
        body_.omega = body_.phi = body_.psi = body_.xm = body_.zm = 0;
        body_.ym = spot_pico::STANCE_DEPTH;
        for (int fw = 0; fw < spot_pico::LEG_COUNT; ++fw) {
            const int leg = spot_pico::FIRMWARE_TO_SIM_LEG[fw];
            const float *home = STANCE_FEET.at[leg];
            const float *foot = feet.at[leg];
            body_.feet[fw][0] = kin->default_feet_positions[fw][0] - (foot[1] - home[1]);
            body_.feet[fw][1] = kin->default_feet_positions[fw][1] + (foot[2] - home[2]);
            body_.feet[fw][2] = kin->default_feet_positions[fw][2] + (foot[0] - home[0]);
            body_.feet[fw][3] = 1;
        }
    }

    // World from the spot_pico base: the IMU's world-from-body rotation times the base axes in the body frame
    // (base X = body y, base Y = -body x, base Z = body z).
    void worldFromBase(float R[3][3]) const {
        const float w = imu_.quat[0], x = imu_.quat[1], y = imu_.quat[2], z = imu_.quat[3];
        const float B[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                               {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                               {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
        for (int r = 0; r < 3; ++r) {
            R[r][0] = B[r][1];
            R[r][1] = -B[r][0];
            R[r][2] = B[r][2];
        }
    }

    static float yawOf(const float R[3][3]) { return std::atan2(R[1][0], R[0][0]); }
};

#else
#define WALK_NN_POLICY 0
#endif
