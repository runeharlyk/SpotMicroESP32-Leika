#pragma once

#include <motion_states/state.h>
#include <utils/math_utils.h>
#include <array>
#include <functional>

struct gait_state_t {
    float step_height {KinConfig::default_step_height};
    float step_x {0};
    float step_z {0};
    float step_angle {0};
    float step_velocity {0.5};
    float step_depth {KinConfig::default_step_depth};
};

enum class WALK_GAIT { TROT, CRAWL };

class WalkState : public MotionState {
  private:
    WALK_GAIT mode = WALK_GAIT::TROT;
    float phase_time = 0.0f;
    float phase_offset[4] = {0.f, 0.5f, 0.5f, 0.f};
    float stand_offset = 0.6f;
    float step_length = 0.0f;
    float speed_factor = 2;
    gait_state_t gait_state;
    gait_state_t target_gait_state;

    struct ShiftState {
        float start_x = 0.0f;
        float start_z = 0.0f;
        float target_x = 0.0f;
        float target_z = 0.0f;
        float start_time = 0.0f;
        int current_shift_leg = -1;
    } shift_state;

    struct LegStates {
        std::array<int, 4> stance;
        std::array<int, 4> swing;
        int stance_count = 0;
        int swing_count = 0;
        int next_swing = -1;
        float time_to_lift = INFINITY;
    };

    static constexpr uint8_t BEZIER_POINTS = 12;
    static constexpr std::array<float, BEZIER_POINTS> COMBINATORIAL_VALUES = {
        combinatorial_constexpr(11, 0),  // 1
        combinatorial_constexpr(11, 1),  // 11
        combinatorial_constexpr(11, 2),  // 55
        combinatorial_constexpr(11, 3),  // 165
        combinatorial_constexpr(11, 4),  // 330
        combinatorial_constexpr(11, 5),  // 462
        combinatorial_constexpr(11, 6),  // 462
        combinatorial_constexpr(11, 7),  // 330
        combinatorial_constexpr(11, 8),  // 165
        combinatorial_constexpr(11, 9),  // 55
        combinatorial_constexpr(11, 10), // 11
        combinatorial_constexpr(11, 11)  // 1
    };

    alignas(32) static constexpr float BEZIER_STEPS[12] = {-1.0f, -1.4f, -1.5f, -1.5f, -1.5f, 0.0f,
                                                           0.0f,  0.0f,  1.5f,  1.5f,  1.4f,  1.0f};

    alignas(32) static constexpr float BEZIER_HEIGHTS[12] = {0.0f, 0.0f, 0.9f, 0.9f, 0.9f, 0.9f,
                                                             0.9f, 1.1f, 1.1f, 1.1f, 0.0f, 0.0f};

  public:
    WalkState() = default;
    const char *name() const override { return "Bezier"; }

    float getPhaseTime() const { return phase_time; }
    float getStepLength() const { return step_length; }

    void set_mode_crawl(float duty = 0.85f, std::array<int, 4> order = {3, 0, 2, 1}) {
        mode = WALK_GAIT::CRAWL;
        speed_factor = 0.5;
        stand_offset = duty;
        const float base[4] = {0.f, 0.25f, 0.5f, 0.75f};
        for (int i = 0; i < 4; ++i) phase_offset[order[i]] = base[i];
    }

    void set_mode_trot(float duty = 0.6f, std::array<float, 4> offsets = {0.f, 0.5f, 0.5f, 0.f}) {
        mode = WALK_GAIT::TROT;
        speed_factor = 2;
        stand_offset = duty;
        for (int i = 0; i < 4; ++i) phase_offset[i] = std::fmod(std::fabs(offsets[i]), 1.f);
    }

    void step(body_state_t &body_state, float dt = 0.02f) override {
        body_state.ym = lerp(body_state.ym, target_body_state.ym, default_smoothing_factor);
        body_state.psi = lerp(body_state.psi, target_body_state.psi, default_smoothing_factor);
        gait_state.step_height = target_gait_state.step_height;
        gait_state.step_x = lerp(gait_state.step_x, target_gait_state.step_x, default_smoothing_factor);
        gait_state.step_z = lerp(gait_state.step_z, target_gait_state.step_z, default_smoothing_factor);
        gait_state.step_velocity = target_gait_state.step_velocity;
        gait_state.step_angle = lerp(gait_state.step_angle, target_gait_state.step_angle, default_smoothing_factor);
        gait_state.step_depth = lerp(gait_state.step_depth, target_gait_state.step_depth, default_smoothing_factor);

        step_length = std::hypot(gait_state.step_x, gait_state.step_z);
        if (gait_state.step_x < 0.0f) step_length = -step_length;
        updatePhase(dt);
        updateBodyPosition(body_state, dt);
        updateFeetPositions(body_state);
    }

  protected:
    void handleCommand(const CommandMsg &cmd) override {
        target_body_state.ym = KinConfig::min_body_height + cmd.h * KinConfig::body_height_range;
        target_body_state.psi = cmd.ry * KinConfig::max_pitch;
        target_gait_state.step_height = cmd.s1 * KinConfig::max_step_height;
        target_gait_state.step_x = cmd.ly * KinConfig::max_step_length;
        target_gait_state.step_z = -cmd.lx * KinConfig::max_step_length;
        target_gait_state.step_velocity = cmd.s;
        target_gait_state.step_angle = cmd.rx;
        target_gait_state.step_depth = KinConfig::default_step_depth;
    }

    static inline bool isZero(float num) { return std::fabs(num) < 0.01; }

    void updatePhase(float dt) {
        if (isZero(gait_state.step_x) && isZero(gait_state.step_z) && isZero(gait_state.step_angle)) {
            phase_time = 0;
            return;
        }
        phase_time = std::fmod(phase_time + dt * gait_state.step_velocity * speed_factor, 1.0f);
    }

    LegStates getLegStates() {
        LegStates states;
        float min_time_to_swing = INFINITY;

        for (int i = 0; i < 4; i++) {
            float phase = std::fmod(phase_time + phase_offset[i], 1.0f);

            if (phase <= stand_offset) {
                states.stance[states.stance_count++] = i;
                float time_to_swing = stand_offset - phase;
                if (time_to_swing < min_time_to_swing) {
                    min_time_to_swing = time_to_swing;
                    states.next_swing = i;
                }
            } else {
                states.swing[states.swing_count++] = i;
            }
        }

        states.time_to_lift = min_time_to_swing;
        return states;
    }

    std::array<float, 3> stanceCentroid(const LegStates &states) {
        if (states.stance_count == 0) {
            return {0.0f, 0.0f, 0.0f};
        }

        float sx = 0.0f, sz = 0.0f;
        int remaining_count = 0;

        for (int i = 0; i < states.stance_count; i++) {
            int leg = states.stance[i];
            if (leg != states.next_swing) {
                sx += default_feet_pos[leg][0];
                sz += default_feet_pos[leg][2];
                remaining_count++;
            }
        }

        if (remaining_count > 0) {
            return {sx / remaining_count, 0.0f, sz / remaining_count};
        }

        return {0.0f, 0.0f, 0.0f};
    }

    static float lerp(float a, float b, float t) { return a + (b - a) * t; }

    void updateBodyPosition(body_state_t &body_state, float dt) {
        if (mode != WALK_GAIT::CRAWL) return;

        const bool moving = !isZero(gait_state.step_x) || !isZero(gait_state.step_z) || !isZero(gait_state.step_angle);
        if (!moving) return;

        LegStates leg_states = getLegStates();

        if (leg_states.stance_count >= 3 && leg_states.swing_count == 0 && leg_states.next_swing != -1) {
            if (shift_state.current_shift_leg != leg_states.next_swing) {
                shift_state.current_shift_leg = leg_states.next_swing;
                shift_state.start_x = body_state.xm;
                shift_state.start_z = body_state.zm;

                auto target = stanceCentroid(leg_states);
                shift_state.target_x = target[0];
                shift_state.target_z = target[2];

                shift_state.start_time = leg_states.time_to_lift;
            }

            float total_time = shift_state.start_time;
            float progress = total_time > 0 ? 1.0f - (leg_states.time_to_lift / total_time) : 1.0f;
            float smooth_progress = smoothstep01(std::clamp(progress, 0.0f, 1.0f));

            body_state.xm = lerp(shift_state.start_x, shift_state.target_x, smooth_progress);
            body_state.zm = lerp(shift_state.start_z, shift_state.target_z, smooth_progress);
        }
    }

    static float smoothstep01(float t) {
        const float x = std::clamp(t, 0.f, 1.f);
        return x * x * (3.f - 2.f * x);
    }

    void updateFeetPositions(body_state_t &body_state) {
        for (int i = 0; i < 4; ++i) updateFootPosition(body_state, i);
    }

    void updateFootPosition(body_state_t &body_state, const int index) {
        body_state.feet[index][0] = this->default_feet_pos[index][0];
        body_state.feet[index][1] = this->default_feet_pos[index][1];
        body_state.feet[index][2] = this->default_feet_pos[index][2];
        const float leg_phase = std::fmod(phase_time + phase_offset[index], 1.0f);
        const bool contact = leg_phase <= stand_offset;
        if (contact)
            standController(body_state, index, leg_phase / stand_offset);
        else
            swingController(body_state, index, (leg_phase - stand_offset) / (1.f - stand_offset));
    }

    void standController(body_state_t &body_state, const int index, const float phase) {
        controller(index, body_state, phase, stanceCurve, &gait_state.step_depth);
    }

    void swingController(body_state_t &body_state, const int index, const float phase) {
        controller(index, body_state, phase, bezierCurve, &gait_state.step_height);
    }

    void controller(const int index, body_state_t &body_state, const float phase,
                    std::function<void(float, float, float *, float, float *)> curve, float *arg) {
        float delta_pos[3] = {0};
        float delta_rot[3] = {0};

        float length = step_length * 0.5f;
        float angle = std::atan2(gait_state.step_z, step_length);
        curve(length, angle, arg, phase, delta_pos);

        length = gait_state.step_angle * 2.0f;
        angle = yawArc(default_feet_pos[index], body_state.feet[index]);
        curve(length, angle, arg, phase, delta_rot);

        body_state.feet[index][0] += delta_pos[0] + delta_rot[0] * 0.2;
        if (step_length || gait_state.step_angle) body_state.feet[index][1] += delta_pos[1] + delta_rot[1] * 0.2;
        body_state.feet[index][2] += delta_pos[2] + delta_rot[2] * 0.2;
    }

    static void stanceCurve(const float length, const float angle, const float *depth, const float phase,
                            float *point) {
        float step = length * (1.0f - 2.0f * phase);
        point[0] += step * std::cos(angle);
        point[2] += step * std::sin(angle);
        if (length != 0.0f) point[1] = -*depth * std::cos((M_PI * (point[0] + point[2])) / (2.f * length));
    }

    static void bezierCurve(const float length, const float angle, const float *height, const float phase,
                            float *point) {
        const float X_POLAR = std::cos(angle);
        const float Z_POLAR = std::sin(angle);

        const float t = std::clamp(phase, 1e-4f, 1.f - 1e-4f);
        float phase_power = 1.0f;
        float inv_phase_power = std::pow(1.0f - t, 11);
        const float one_minus_phase = 1.0f - t;

        for (int i = 0; i < 12; i++) {
            float b = COMBINATORIAL_VALUES[i] * phase_power * inv_phase_power;
            point[0] += b * BEZIER_STEPS[i] * length * X_POLAR;
            point[1] += b * BEZIER_HEIGHTS[i] * *height;
            point[2] += b * BEZIER_STEPS[i] * length * Z_POLAR;

            phase_power *= phase;
            inv_phase_power /= one_minus_phase;
        }
    }

    static float yawArc(const float feet_pos[3], const float *current_pos) {
        const float foot_mag = std::hypot(feet_pos[0], feet_pos[2]);
        const float foot_dir = std::atan2(feet_pos[2], feet_pos[0]);
        const float offsets[] = {current_pos[0] - feet_pos[0], current_pos[1] - feet_pos[1],
                                 current_pos[2] - feet_pos[2]};
        const float offset_mag = std::hypot(offsets[0], offsets[2]);
        const float offset_mod = std::atan2(offset_mag, foot_mag);

        return (float)M_PI_2 + foot_dir + offset_mod;
    }
};