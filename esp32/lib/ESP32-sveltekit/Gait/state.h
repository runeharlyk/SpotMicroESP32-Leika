#pragma once

#include <Kinematics.h>

struct gait_state_t {
    float step_height;
    float step_x;
    float step_z;
    float step_angle;
    float step_velocity;
    float step_depth;
};

struct ControllerCommand {
    int stop;
    float lx, ly, rx, ry, h, s, s1;
};

class GaitState {
  protected:
    virtual const char *name() const = 0;
    float default_feet_pos[4][4] = {{1, -1, 0.7, 1}, {1, -1, -0.7, 1}, {-1, -1, 0.7, 1}, {-1, -1, -0.7, 1}};
    gait_state_t gait_state = {0.4, 0, 0, 0, 1, 0.002};

    void mapCommand(ControllerCommand command) {
        this->gait_state.step_height = 0.4 + (command.s1 / 128 + 1) / 2;
        this->gait_state.step_x = command.ly / 128;
        this->gait_state.step_z = -command.lx / 128;
        this->gait_state.step_velocity = command.s / 128 + 1;
        this->gait_state.step_angle = command.rx / 128;
        this->gait_state.step_depth = 0.002;
    }

  public:
    virtual float getDefaultHeight() const { return 0.5f; }

    virtual void begin() { ESP_LOGI("Gait Planner", "Starting %s", name()); }

    virtual void end() { ESP_LOGI("Gait Planner", "Ending %s", name()); }

    virtual void step(body_state_t &body_state, ControllerCommand command, float dt = 0.02f) {
        this->mapCommand(command);
    }
};