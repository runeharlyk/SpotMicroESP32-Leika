#pragma once

#include <motion_states/state.h>

class StandState : public MotionState {
  protected:
    const char *name() const override { return "Stand"; }

    virtual void begin() {
        target_body_state.xm = 0;
        target_body_state.ym = KinConfig::min_body_height + 0.5 * KinConfig::body_height_range;
        target_body_state.zm = 0;
        target_body_state.omega = 0;
        target_body_state.phi = 0;
        target_body_state.psi = 0;
        target_body_state.updateFeet(KinConfig::default_feet_positions);
    }

    void handleCommand(const CommandMsg &cmd) override {
        target_body_state.ym = KinConfig::min_body_height + cmd.h * KinConfig::body_height_range;
        target_body_state.psi = cmd.ry * KinConfig::max_pitch;
        target_body_state.phi = cmd.rx * KinConfig::max_roll;
        target_body_state.xm = cmd.ly * KinConfig::max_body_shift_x;
        target_body_state.zm = cmd.lx * KinConfig::max_body_shift_z;
        target_body_state.updateFeet(KinConfig::default_feet_positions);
    }

    void step(body_state_t &body_state, float dt = 0.02f) override {
        lerpToBody(body_state);
        updateFeet(body_state);
    }
};