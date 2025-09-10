#pragma once

#include <motion_skills/walk_skill.h>

class SpinAroundSkill : public WalkSkill {
  private:
    bool _clockwise = true;

  public:
    SpinAroundSkill(bool clockwise = true) : WalkSkill(0.0f, 0.0f, clockwise ? 90.0f : -90.0f), _clockwise(clockwise) {}

    const char* getName() const override { return _clockwise ? "Spin Clockwise" : "Spin Counter-Clockwise"; }
};
