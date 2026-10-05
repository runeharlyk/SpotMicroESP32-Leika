#ifndef MotionService_h
#define MotionService_h

#include "esp_timer.h"

#include <kinematics.h>
#include <peripherals/gesture.h>
#include <peripherals/imu/imu_math.h>
#include <utils/timing.h>
#include <utils/math_utils.h>

#include <motion_states/state.h>
#include <motion_states/walk_state.h>
#include <motion_states/stand_state.h>
#include <motion_states/rest_state.h>
#include <motion_states/animate_state.h>
#include <message_types.h>
#include <motion_inbox.h>
#include <variant.h>
#include <atomic>
#include <mutex>
#include <optional>
#include <utility>

enum class MOTION_STATE { DEACTIVATED, IDLE, CALIBRATION, REST, STAND, WALK, ANIMATE };

class MotionService {
  public:
    /** The variant's geometry, set before the tasks start; without one, every mode but DEACTIVATED is refused. */
    void useConfig(const KinConfig* config);

    void begin();

    void handleAngles(const socket_message_AnglesData& data);

    /** Input, mode and gait from the socket's task; the control task applies them in update(). */
    MotionInbox inbox;

    void setState(MotionState* newState);

    void handleGestures(const gesture_t ges);

    bool update(const ImuSample& imu, gesture_t gesture);

    /** Whether update() applied a mode message since the last call. */
    bool takeModeApplied() { return std::exchange(modeApplied, false); }

    /** The variant update() switched to since the last call, if any. */
    std::optional<KinematicsVariant> takeVariantApplied() { return std::exchange(variantApplied, std::nullopt); }

    bool update_angles(float new_angles[12], float angles[12]);

    float* getAngles() { return angles; }

    inline bool isActive() { return state != nullptr; }

    /** The mode and gait in force, for the socket's task to report: the robot, not the app, owns them. */
    socket_message_ModesEnum mode() const { return currentMode.load(); }
    socket_message_WalkGaits gait() const { return currentGait.load(); }

    /** The sign applied to each joint's angle on its way to the servo. */
    static constexpr float JOINT_DIRECTION[12] = {1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1};

    const CommandMsg& currentCommand() const { return command; }
    /** When the command in use arrived, in esp_timer microseconds; 0 before the first. */
    int64_t commandReceivedAt() const { return commandRxUs; }
    /** Whether the dead-man stop is in force: the link fell silent and no input came since. */
    bool linkLost() const { return linkLostNow; }

    /** The clip playing or last played, for the socket's task to report. */
    struct AnimationStatus {
        char name[anim::NAME_LEN_MAX + 1];
        anim::State state;
        float t;
        uint32_t clampedMask;
    };
    AnimationStatus animationStatus() {
        std::lock_guard<std::mutex> lock(animationStatusMutex);
        return animationStatusNow;
    }

  private:
    void applyMail(const MotionInbox::Mail& mail);
    // Every mode change, from the app or a gesture, goes through here.
    void setMode(socket_message_ModesEnum mode);
    void applyMode(socket_message_ModesEnum mode);
    void startAnimation(const MotionInbox::Play &play);
    void recordAnimationStatus();
    void stopLocomotion();
    void switchVariant(KinematicsVariant variant);

    std::atomic<socket_message_ModesEnum> currentMode {socket_message_ModesEnum_DEACTIVATED};
    std::atomic<socket_message_WalkGaits> currentGait {socket_message_WalkGaits_TROT};

    const KinConfig* config = nullptr;
    std::optional<Kinematics> kinematics;
    bool modeApplied = false;
    std::optional<KinematicsVariant> variantApplied;

    CommandMsg command = {0, 0, 0, 0, 0, 0, 0};
    int64_t commandRxUs = 0;
    bool linkLostNow = false;

    friend class MotionState;

    MotionState* state = nullptr;

    RestState restState;
    AnimateState animateState;
    // The mode a clip hands back to when it ends: the one it started from, stand instead of walk, or one asked for
    // while it played.
    socket_message_ModesEnum returnMode = socket_message_ModesEnum_STAND;
    std::mutex animationStatusMutex;
    AnimationStatus animationStatusNow {"", anim::State::IDLE, 0, 0};
    StandState standState;
    WalkState walkState;

    body_state_t body_state;

    float new_angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    float angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    int64_t lastUpdate = esp_timer_get_time();
};

#endif
