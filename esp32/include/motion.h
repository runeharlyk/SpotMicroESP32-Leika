#ifndef MotionService_h
#define MotionService_h

#include <ArduinoJson.h>

#include <kinematics.h>
#include <peripherals/gesture.h>
#include <peripherals/peripherals.h>
#include <utils/timing.h>
#include <utils/math_utils.h>

#include <motion_states/state.h>
#include <motion_states/walk_state.h>
#include <motion_states/stand_state.h>
#include <motion_states/rest_state.h>
#include <message_types.h>

enum class MOTION_STATE { DEACTIVATED, IDLE, CALIBRATION, REST, STAND, WALK };

class MotionService {
  public:
    void begin();

    void anglesEvent(JsonVariant &root, int originId);

    void handleInput(JsonVariant &root, int originId);

    void handleWalkGait(JsonVariant &root, int originId);

    void handleMode(JsonVariant &root, int originId);

    void setState(MotionState *newState);

    void handleGestures(const gesture_t ges);

    bool update(Peripherals *peripherals);

    bool update_angles(float new_angles[12], float angles[12]);

    float *getAngles() { return angles; }

    inline bool isActive() { return state != nullptr; }

  private:
    Kinematics kinematics;

    CommandMsg command = {0, 0, 0, 0, 0, 0, 0};

    friend class MotionState;

    MotionState *state = nullptr;

    RestState restState;
    StandState standState;
    WalkState walkState;

    body_state_t body_state;

    float new_angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    float angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    float dir[12] = {1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1};

    unsigned long lastUpdate = millis();
};

#endif
