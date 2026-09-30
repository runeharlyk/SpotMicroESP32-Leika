#ifndef MotionService_h
#define MotionService_h

#include "esp_timer.h"

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
#include <motion_inbox.h>
#include <utility>

enum class MOTION_STATE { DEACTIVATED, IDLE, CALIBRATION, REST, STAND, WALK };

class MotionService {
  public:
    void begin();

    void handleAngles(const socket_message_AnglesData& data);

    /** Input, mode and gait from the socket's task; the control task applies them in update(). */
    MotionInbox inbox;

    void setState(MotionState* newState);

    void handleGestures(const gesture_t ges);

    bool update(Peripherals* peripherals);

    /** Whether update() applied a mode message since the last call. */
    bool takeModeApplied() { return std::exchange(modeApplied, false); }

    bool update_angles(float new_angles[12], float angles[12]);

    float* getAngles() { return angles; }

    inline bool isActive() { return state != nullptr; }

  private:
    void applyMail(const MotionInbox::Mail& mail);
    void applyMode(socket_message_ModesEnum mode);
    void stopLocomotion();

    Kinematics kinematics;
    bool modeApplied = false;

    CommandMsg command = {0, 0, 0, 0, 0, 0, 0};

    friend class MotionState;

    MotionState* state = nullptr;

    RestState restState;
    StandState standState;
    WalkState walkState;

    body_state_t body_state;

    float new_angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    float angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    float dir[12] = {1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1};

    int64_t lastUpdate = esp_timer_get_time();
};

#endif
