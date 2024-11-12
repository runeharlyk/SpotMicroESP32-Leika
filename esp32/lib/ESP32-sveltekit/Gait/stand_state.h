#pragma once

#include <gait/state.h>

class StandState : public GaitState {
  protected:
    const char *name() const override { return "Stand"; }

    void step(body_state_t &body_state, ControllerCommand command, float dt = 0.02f) override {
        body_state.omega = 0;
        body_state.phi = command.rx / 8;
        body_state.psi = command.ry / 8;
        body_state.xm = command.ly / 2 / 100;
        body_state.zm = command.lx / 2 / 100;
        body_state.updateFeet(default_feet_pos);
    }
};