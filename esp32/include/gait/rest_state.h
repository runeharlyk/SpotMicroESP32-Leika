#pragma once

#include <gait/state.h>

class RestState : public GaitState {
  protected:
    const char *name() const override { return "Rest"; }

    void step(body_state_t &body_state, CommandMsg command, float dt = 0.02f) override {
        body_state.omega = 0;
        body_state.phi = 0;
        body_state.psi = 0;
        body_state.xm = 0;
        body_state.ym = KinConfig::default_body_height / 2;
        body_state.zm = 0;
        body_state.updateFeet(default_feet_pos);
    }
};