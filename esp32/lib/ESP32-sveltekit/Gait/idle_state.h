#pragma once

#include <gait/gait_state.h>

class IdleState : public GaitState {
  protected:
    const char *name() const override { return "Idle"; }
};