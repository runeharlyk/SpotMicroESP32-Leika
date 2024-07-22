#ifndef GAIT_STATE_H
#define GAIT_STATE_H

#include <Kinematics.h>

class GaitState {
  private:
  public:
    GaitState() { ESP_LOGI("GaitState", "%s constructor", name); }

    virtual ~GaitState() = 0;

    virtual void begin() = 0;

    virtual void end() = 0;

    virtual void loop() = 0;

    const char *name;

    static body_state_t body_state;
};

class IdleState : public GaitState {
  public:
    IdleState() { name = "Idle"; }
    ~IdleState() {}

    void begin() override { ESP_LOGI("GaitState", "IdleState begin"); }

    void end() override { ESP_LOGI("GaitState", "IdleState end"); }

    void loop() override { ESP_LOGI("GaitState", "IdleState loop"); }
};

class StandState : public GaitState {
  public:
    StandState() { name = "Stand"; }
    ~StandState() {}

    void begin() override { ESP_LOGI("GaitState", "StandState begin"); }

    void end() override { ESP_LOGI("GaitState", "StandState end"); }

    void loop() override { ESP_LOGI("GaitState", "StandState loop"); }
};

class WalkState : public GaitState {
  public:
    WalkState() { name = "Walk"; }
    ~WalkState() {}

    void begin() override { ESP_LOGI("GaitState", "WalkState begin"); }

    void end() override { ESP_LOGI("GaitState", "WalkState end"); }

    void loop() override { ESP_LOGI("GaitState", "WalkState loop"); }
};

#endif