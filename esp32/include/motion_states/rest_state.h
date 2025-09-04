#pragma once

#include <motion_states/state.h>

class RestState : public MotionState {
  protected:
    const char *name() const override { return "Rest"; }

    virtual void begin() {
        target_body_state.xm = 0;
        target_body_state.ym = KinConfig::min_body_height;
        target_body_state.zm = 0;
        target_body_state.omega = 0;
        target_body_state.phi = 0;
        target_body_state.psi = 0;
        target_body_state.updateFeet(KinConfig::default_feet_positions);
    }

    void step(body_state_t &body_state, float dt = 0.02f) override {
        lerpToBody(body_state);
        updateFeet(body_state);
    }
};