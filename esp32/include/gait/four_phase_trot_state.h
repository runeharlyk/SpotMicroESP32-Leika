#pragma once

#include <gait/phase_state_base.h>

class FourPhaseWalkState : public PhaseGaitState {
  protected:
    const char *name() const override { return "Four phase walk"; }

    int num_phases() const override { return 4; }

    float phase_speed_factor() const override { return 6; }

    float swing_stand_ratio() const override { return 1.0f / (num_phases() - 1); }

  public:
    FourPhaseWalkState() {
        uint8_t contact[4][4] = {{1, 0, 1, 1}, {1, 1, 1, 0}, {1, 1, 1, 0}, {1, 0, 1, 1}};
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                contact_phases[i][j] = contact[i][j];
            }
        }
    }

    void step(body_state_t &body_state, ControllerCommand command, float dt = 0.02f) override {
        return PhaseGaitState::step(body_state, command, dt);
    }
};