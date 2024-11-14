#pragma once

#include <gait/state.h>
#include <utils/math_utils.h>
#include <array>
#include <functional>

class BezierState : public GaitState {
  private:
    float phase_time = 0.0f;
    static constexpr float PHASE_OFFSET[4] = {0.f, 0.5f, 0.5f, 0.f};
    static constexpr float STAND_OFFSET = 0.75f;
    static constexpr uint8_t BEZIER_POINTS = 12;
    float step_length = 0.0f;
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

    void step(body_state_t &body_state, ControllerCommand command, float dt = 0.02f) override {
        this->mapCommand(command);
        step_length = std::hypot(gait_state.step_x, gait_state.step_z);
        if (gait_state.step_x < 0.0f) {
            step_length = -step_length;
        }
        updatePhase(dt);
        updateFeetPositions(body_state);
    }

  protected:
    void updatePhase(float dt) { phase_time = std::fmod(phase_time + dt * gait_state.step_velocity * 2, 1.0f); }

    void updateFeetPositions(body_state_t &body_state) {
        for (int i = 0; i < 4; ++i) {
            updateFootPosition(body_state, i);
        }
    }

    void updateFootPosition(body_state_t &body_state, const int index) {
        body_state.feet[index][0] = this->default_feet_pos[index][0];
        body_state.feet[index][1] = this->default_feet_pos[index][1];
        body_state.feet[index][2] = this->default_feet_pos[index][2];
        const float leg_phase = std::fmod(phase_time + PHASE_OFFSET[index], 1.0f);
        const bool contact = leg_phase <= STAND_OFFSET;
        contact ? standController(body_state, index, leg_phase / 0.75)
                : swingController(body_state, index, (leg_phase - 0.75) / (1 - 0.75));
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

        float length = step_length / 2.0f;
        float angle = std::atan2(gait_state.step_z, step_length) * 2;
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

        float phase_power = 1.0f;
        float inv_phase_power = std::pow(1.0f - phase, 11);
        const float one_minus_phase = 1.0f - phase;

        for (int i = 0; i < 12; i++) {
            float b = COMBINATORIAL_VALUES[i] * phase_power * inv_phase_power;
            point[0] += b * BEZIER_STEPS[i] * length * X_POLAR;
            point[1] += b * BEZIER_HEIGHTS[i] * *height;
            point[2] += b * BEZIER_STEPS[i] * length * Z_POLAR;

            phase_power *= phase;
            inv_phase_power /= one_minus_phase;
        }
    }

    static float yawArc(const float feet_pos[4], const float *current_pos) {
        const float foot_mag = std::hypot(feet_pos[0], feet_pos[2]);
        const float foot_dir = std::atan2(feet_pos[2], feet_pos[0]);
        const float offsets[] = {current_pos[0] - feet_pos[0], current_pos[1] - feet_pos[1],
                                 current_pos[2] - feet_pos[2]};
        const float offset_mag = std::hypot(offsets[0], offsets[2]);
        const float offset_mod = std::atan2(offset_mag, foot_mag);

        return M_PI_2 + foot_dir + offset_mod;
    }
};