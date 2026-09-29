#pragma once

#include <template/stateful_service.h>
#include <template/stateful_persistence.h>
#include <settings/robot_settings.h>

class RobotService : public StatefulService<RobotSettings> {
  public:
    RobotService();

    void begin();

    const char *name() { return state().name; }

    /** Returns false when the name is invalid; the stored name is then unchanged. */
    bool rename(const char *name);

  private:
    FSPersistencePB<RobotSettings> _persistence;
};
