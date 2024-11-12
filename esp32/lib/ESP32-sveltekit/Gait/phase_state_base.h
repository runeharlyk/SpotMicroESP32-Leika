#pragma once

#include <gait/state.h>

class PhaseGaitState : public GaitState {
  protected:
    int phase = 0;
    float phase_time = 0;
    virtual int num_phases() const = 0;
    virtual float phase_speed_factor() const = 0;
    virtual float swing_stand_ratio() const = 0;
    float dt = 0.02f;

    uint8_t contact_phases[4][8];
    float shifts[4][3];

    void step(body_state_t &body_state, ControllerCommand command, float dt = 0.02f) override {
        mapCommand(command);
        this->dt = dt;
        updatePhase();
        updateBodyPosition(body_state);
        updateFeetPositions(body_state);
    }

    void updatePhase() {
        phase_time += dt * phase_speed_factor() * gait_state.step_velocity;

        if (phase_time >= 1.0f) {
            phase += 1;
            if (phase == num_phases()) phase = 0;
            phase_time = 0;
        }
    }

    void updateBodyPosition(body_state_t &body_state) {
        if (num_phases() == 4) return;

        const auto &shift = shifts[phase / 2];
        body_state.xm += (shift[0] - body_state.xm) * dt * 4;
        body_state.zm += (shift[2] - body_state.zm) * dt * 4;
    }

    void updateFeetPositions(body_state_t &body_state) {
        for (int i = 0; i < 4; ++i) {
            updateFootPosition(body_state, i);
        }
    }

    void updateFootPosition(body_state_t &body_state, int index) {
        bool contact = contact_phases[index][phase];
        contact ? stand(body_state, index) : swing(body_state, index);
    }

    void stand(body_state_t &body_state, int index) {
        float delta_pos[3] = {-gait_state.step_x * dt * swing_stand_ratio(), 0,
                              -gait_state.step_z * dt * swing_stand_ratio()};

        body_state.feet[index][0] += delta_pos[0];
        body_state.feet[index][1] = default_feet_pos[index][1];
        body_state.feet[index][2] += delta_pos[2];
    }

    void swing(body_state_t &body_state, int index) {
        float delta_pos[3] = {gait_state.step_x * dt, 0, gait_state.step_z * dt};

        if (std::fabs(gait_state.step_x) < 0.01) {
            delta_pos[0] = (default_feet_pos[index][0] - body_state.feet[index][0]) * dt * 8;
        }

        if (std::fabs(gait_state.step_z) < 0.01) {
            delta_pos[2] = (default_feet_pos[index][2] - body_state.feet[index][2]) * dt * 8;
        }

        body_state.feet[index][0] += delta_pos[0];
        body_state.feet[index][1] = default_feet_pos[index][1] + std::sin(phase_time * M_PI) * gait_state.step_height;
        body_state.feet[index][2] += delta_pos[2];
    }
};