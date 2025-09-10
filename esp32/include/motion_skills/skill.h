#pragma once

#include <kinematics.h>
#include <message_types.h>
#include <peripherals/peripherals.h>
#include <motion_states/state.h>

class Skill {
  public:
    virtual ~Skill() = default;

    virtual const char* getName() const = 0;

    virtual void begin(body_state_t& body_state, Peripherals* peripherals) {}

    virtual void execute(body_state_t& body_state, MotionState* currentState, Peripherals* peripherals, float dt) = 0;

    virtual bool isComplete() const = 0;

    virtual void reset() = 0;

    virtual MotionState* getRequiredState() = 0;

  protected:
    bool _isActive = false;
    bool _isComplete = false;
};
