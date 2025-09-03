#pragma once

#include <gait/state.h>
#include <utils/math_utils.h>
#include <array>
#include <functional>

enum class WALK_GAIT { TROT, CRAWL };

class WalkState : public GaitState {
  private:
    WALK_GAIT mode = WALK_GAIT::TROT;
    float phase_time = 0.0f;
    float phase_offset[4] = {0.f, 0.5f, 0.5f, 0.f};
    float stand_offset = 0.6f;
    float step_length = 0.0f;
    float phase_lead = 0.08f;
    float feather = 0.05f;
    float speed_factor = 0.5;
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
    const char *name() const override { return "Bezier"; }

    void set_mode_crawl(float duty = 0.85f, std::array<int, 4> order = {0, 3, 1, 2}) {
        mode = WALK_GAIT::CRAWL;
        speed_factor = 0.1;
        stand_offset = duty;
        const float base[4] = {0.f, 0.25f, 0.5f, 0.75f};
        for (int i = 0; i < 4; ++i) phase_offset[order[i]] = base[i];
    }

    void set_mode_trot(float duty = 0.6f, std::array<float, 4> offsets = {0.f, 0.5f, 0.5f, 0.f}) {
        mode = WALK_GAIT::TROT;
        speed_factor = 0.5;
        stand_offset = duty;
        for (int i = 0; i < 4; ++i) phase_offset[i] = std::fmod(std::fabs(offsets[i]), 1.f);
    }

    void step(body_state_t &body_state, CommandMsg command, float dt = 0.02f) override {
        this->mapCommand(command);
        step_length = std::hypot(gait_state.step_x, gait_state.step_z);
        if (gait_state.step_x < 0.0f) {
            step_length = -step_length;
        }
        updatePhase(dt);
        updateBodyPosition(body_state, dt);
        updateFeetPositions(body_state);
    }

  protected:
    void updatePhase(float dt) {
        phase_time = std::fmod(phase_time + dt * gait_state.step_velocity * speed_factor, 1.0f);
    }

    void updateBodyPosition(body_state_t &body_state, float dt) {
        if (mode != WALK_GAIT::CRAWL) return;
        const bool moving = gait_state.step_x != 0.f || gait_state.step_z != 0.f || gait_state.step_angle != 0.f;
        if (!moving) return;
        const auto c = dynamicStanceCentroid(body_state);
        const float a = 1.f - std::exp(-16.f * dt);
        body_state.xm += (c[0] - body_state.xm) * a;
        body_state.zm += (c[2] - body_state.zm) * a;
    }

    std::array<float, 3> dynamicStanceCentroid(const body_state_t &body_state) const {
        if (mode != WALK_GAIT::CRAWL) return {body_state.xm, 0.f, body_state.zm};
        float sx = 0.f, sz = 0.f;
        int n = 0;
        for (int i = 0; i < 4; ++i) {
            float p = std::fmod(phase_time + phase_offset[i], 1.f);
            if (p < 0.f) p += 1.f;
            if (p <= stand_offset) {
                sx += default_feet_pos[i][0];
                sz += default_feet_pos[i][2];
                ++n;
            }
        }
        if (n == 0) return {body_state.xm, 0.f, body_state.zm};
        return {sx / n, 0.f, sz / n};
    }

    static float smoothstep01(float t) {
        const float x = std::clamp(t, 0.f, 1.f);
        return x * x * (3.f - 2.f * x);
    }

    float stanceWeight(int i) const {
        const float s = stand_offset;
        const float e = feather;
        float p = std::fmod(phase_time + phase_offset[i] + phase_lead, 1.f);
        if (p < 0.f) p += 1.f;
        if (p < s - e) return 1.f;
        if (p > s + e && p < 1.f - e) return 0.f;
        if (p <= s + e) {
            const float t = (p - (s - e)) / (2.f * e);
            return 1.f - smoothstep01(t);
        }
        const float q = p >= 1.f - e ? (p - (1.f - e)) / e : (e - p) / e;
        return smoothstep01(q);
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
        if (mode == WALK_GAIT::CRAWL)
            controller(index, body_state, phase, stanceCurveFlat, &gait_state.step_depth);
        else
            controller(index, body_state, phase, stanceCurve, &gait_state.step_depth);
    }

    void swingController(body_state_t &body_state, const int index, const float phase) {
        controller(index, body_state, phase, bezierCurve, &gait_state.step_height);
    }

    static void stanceCurveFlat(const float length, const float angle, const float *, const float phase, float *point) {
        float step = length * (1.0f - 2.0f * phase);
        point[0] += step * std::cos(angle);
        point[2] += step * std::sin(angle);
        point[1] += 0.f;
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

        if (length != 0.0f) {
            point[1] = -*depth * std::cos((M_PI * (point[0] + point[2])) / (2.f * length));
        }
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