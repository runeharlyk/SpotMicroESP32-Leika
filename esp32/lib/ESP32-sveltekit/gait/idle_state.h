#pragma once

#include <gait/state.h>

class IdleState : public GaitState {
  protected:
    const char *name() const override { return "Idle"; }
};