#pragma once

#include <kinematics.h>
#include <message_types.h>

struct gait_state_t {
    float step_height;
    float step_x;
    float step_z;
    float step_angle;
    float step_velocity;
    float step_depth;
};

class GaitState {
  protected:
    virtual const char *name() const = 0;
    static constexpr const float (&default_feet_pos)[4][4] = Kinematics::default_feet_positions;
    gait_state_t gait_state = {0.4, 0, 0, 0, 1, 0.002};

    virtual void mapCommand(CommandMsg command) {
        this->gait_state.step_height = command.s1 / 2;
        this->gait_state.step_x = command.ly;
        this->gait_state.step_z = -command.lx;
        this->gait_state.step_velocity = command.s;
        this->gait_state.step_angle = command.rx;
        this->gait_state.step_depth = 0.002;
    }

  public:
    virtual float getDefaultHeight() const { return 0.5f; }

    virtual void begin() { ESP_LOGI("Gait Planner", "Starting %s", name()); }

    virtual void end() { ESP_LOGI("Gait Planner", "Ending %s", name()); }

    virtual void step(body_state_t &body_state, CommandMsg command, float dt = 0.02f) { this->mapCommand(command); }
};