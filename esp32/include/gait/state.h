#pragma once

#include <kinematics.h>
#include <gait/kinematic_constraints.h>
#include <message_types.h>

struct gait_state_t {
    float step_height {KinConfig::default_step_height};
    float step_x {0};
    float step_z {0};
    float step_angle {0};
    float step_velocity {0.5};
    float step_depth {KinConfig::default_step_depth};
};

class GaitState {
  protected:
    virtual const char *name() const = 0;
    static constexpr const float (&default_feet_pos)[4][4] = KinConfig::default_feet_positions;

    gait_state_t gait_state;

    virtual void mapCommand(CommandMsg command) {
        this->gait_state.step_height = command.s1 / 2;
        this->gait_state.step_x = command.ly;
        this->gait_state.step_z = -command.lx;
        this->gait_state.step_velocity = command.s;
        this->gait_state.step_angle = command.rx;
        this->gait_state.step_depth = 0.002;
    }

  public:
    virtual void begin() { ESP_LOGI("Gait Planner", "Starting %s", name()); }

    virtual void end() { ESP_LOGI("Gait Planner", "Ending %s", name()); }

    virtual void step(body_state_t &body_state, CommandMsg command, float dt = 0.02f) { this->mapCommand(command); }
};