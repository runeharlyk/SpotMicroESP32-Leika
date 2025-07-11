#pragma once

#include <gait/phase_state_base.h>

class EightPhaseWalkState : public PhaseGaitState {
  protected:
    const char *name() const override { return "Eight phase walk"; }

    int num_phases() const override { return 8; }

    float phase_speed_factor() const override { return 4; }

    float swing_stand_ratio() const override { return 1.0f / (num_phases() - 1); }

  public:
    EightPhaseWalkState() {
        uint8_t contact[4][8] = {
            {1, 0, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 0, 1, 1}, {1, 1, 1, 1, 1, 1, 1, 0}, {1, 1, 1, 0, 1, 1, 1, 1}};
        float shift_values[4][3] = {{-0.05f, 0, -0.2f}, {0.25f, 0, 0.2f}, {-0.05f, 0, 0.2f}, {0.25f, 0, -0.2f}};
        for (uint8_t i = 0; i < 4; ++i) {
            for (uint8_t j = 0; j < 8; ++j) {
                contact_phases[i][j] = contact[i][j];
            }
            for (uint8_t j = 0; j < 3; ++j) {
                shifts[i][j] = shift_values[i][j];
            }
        }
    }

    void step(body_state_t &body_state, ControllerCommand command, float dt = 0.02f) override {
        return PhaseGaitState::step(body_state, command, dt);
    }
};