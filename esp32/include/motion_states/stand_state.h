#pragma once

#include <motion_states/state.h>

class StandState : public MotionState {
  protected:
    const char *name() const override { return "Stand"; }

    virtual void begin() {
        target_body_state.xm = 0;
        target_body_state.ym = kin->min_body_height + 0.5 * kin->body_height_range;
        target_body_state.zm = 0;
        target_body_state.omega = 0;
        target_body_state.phi = 0;
        target_body_state.psi = 0;
        target_body_state.updateFeet(kin->default_feet_positions);
    }

    void handleCommand(const CommandMsg &cmd) override {
        target_body_state.ym = kin->min_body_height + cmd.h * kin->body_height_range;
        target_body_state.psi = cmd.ry * kin->max_pitch;
        target_body_state.phi = cmd.rx * kin->max_roll;
        target_body_state.xm = cmd.ly * kin->max_body_shift_x;
        target_body_state.zm = cmd.lx * kin->max_body_shift_z;
        target_body_state.updateFeet(kin->default_feet_positions);
    }

    void step(body_state_t &body_state, float dt = 0.02f) override {
        smoothToBody(body_state, dt, true);
        easeFeet(body_state, dt);
    }
};