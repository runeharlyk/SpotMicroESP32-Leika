#ifndef MotionService_h
#define MotionService_h

#include <ArduinoJson.h>
#include "esp_timer.h"
#include <functional>

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

using SkillCompleteCallback = std::function<void()>;

class MotionService {
  public:
    void begin();

    void anglesEvent(JsonVariant &root, int originId);

    void handleInput(JsonVariant &root, int originId);

    void handleWalkGait(JsonVariant &root, int originId);

    void handleMode(JsonVariant &root, int originId);

    void handleDisplacement(JsonVariant &root, int originId);

    void handleSkill(JsonVariant &root, int originId);

    void setState(MotionState *newState);

    void handleGestures(const gesture_t ges);

    bool update(Peripherals *peripherals);

    bool update_angles(float new_angles[12], float angles[12]);

    float *getAngles() { return angles; }

    inline bool isActive() { return state != nullptr; }

    void resetDisplacement() { body_state.resetDisplacement(); }

    void setSkillTarget(float x, float z, float yaw) { body_state.skill.set(x, z, yaw); }

    void clearSkill() { body_state.skill.reset(); }

    bool isSkillActive() const { return body_state.skill.active; }

    bool isSkillComplete() const { return body_state.skill.isComplete(); }

    const displacement_state_t &getDisplacement() const { return body_state.cumulative; }

    const skill_target_t &getSkill() const { return body_state.skill; }

    void getDisplacementResult(JsonVariant &root) const {
        root["x"] = body_state.cumulative.x;
        root["y"] = body_state.cumulative.y;
        root["z"] = body_state.cumulative.z;
        root["yaw"] = body_state.cumulative.yaw;
        root["distance"] = body_state.cumulative.distance();
        root["skill_active"] = body_state.skill.active;
        root["skill_target_x"] = body_state.skill.target_x;
        root["skill_target_z"] = body_state.skill.target_z;
        root["skill_target_yaw"] = body_state.skill.target_yaw;
        root["skill_traveled_x"] = body_state.skill.traveled_x;
        root["skill_traveled_z"] = body_state.skill.traveled_z;
        root["skill_rotated"] = body_state.skill.rotated;
        root["skill_progress"] = body_state.skill.progress();
        root["skill_complete"] = body_state.skill.isComplete();
    }

    void setSkillCompleteCallback(SkillCompleteCallback callback) { skillCompleteCallback = callback; }

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

    int64_t lastUpdate = esp_timer_get_time();

    SkillCompleteCallback skillCompleteCallback = nullptr;
    bool skillWasComplete = false;

    void checkSkillComplete();
};

#endif
