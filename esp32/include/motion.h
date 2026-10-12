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
#include <motion_states/walk_nn_state.h>
#include <message_types.h>
#include <motion_inbox.h>
#include <gesture_control.h>
#include <fall_detector.h>
#include <goal_controller.h>
#include <odometry.h>
#include <variant.h>
#include <atomic>
#include <mutex>
#include <optional>
#include <utility>

// Numbered as ModesEnum, which setMode casts.
enum class MOTION_STATE { DEACTIVATED, IDLE, CALIBRATION, REST, STAND, WALK, ANIMATE, WALK_NN };

class MotionService {
  public:
    /** The variant's geometry, set before the tasks start; without one, every mode but DEACTIVATED is refused. */
    void useConfig(const KinConfig* config);

    void begin();

    /** Input, mode and gait from the socket's task; the control task applies them in update(). */
    MotionInbox inbox;

    void setState(MotionState* newState);

    void handleGestures(const gesture_t ges);

    /** The clip a down gesture plays from stand, loaded before the control task starts; without one it rests. */
    void setSitClip(std::shared_ptr<const anim::Clip> clip) { sitClip = std::move(clip); }

    bool update(const ImuSample& imu, gesture_t gesture);

    /** Whether update() applied a mode message since the last call. */
    bool takeModeApplied() { return std::exchange(modeApplied, false); }

    /** The variant update() switched to since the last call, if any. */
    std::optional<KinematicsVariant> takeVariantApplied() { return std::exchange(variantApplied, std::nullopt); }

    bool update_angles(float new_angles[12], float angles[12]);

    float* getAngles() { return angles; }
    const float* getAngles() const { return angles; }

    /** Whether WALK_NN can run at all: the firmware carries a policy and it passed its self-check in begin(). */
    bool walkNNAvailable() const { return walkNNVerified.load(); }

    inline bool isActive() { return state != nullptr; }

    /** The mode and gait in force, for the socket's task to report: the robot, not the app, owns them. */
    socket_message_ModesEnum mode() const { return currentMode.load(); }
    socket_message_WalkGaits gait() const { return currentGait.load(); }

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

    /** Dead reckoning since the last reset or deactivation, the gyro fused in; safe to call from any task. */
    FusedEstimate odometry() const {
        std::lock_guard<std::mutex> lock(odometryMutex);
        return fusedOdometry.estimate();
    }
    /** Starts the dead reckoning afresh, and returns where it had got to. */
    FusedEstimate resetOdometry() {
        std::lock_guard<std::mutex> lock(odometryMutex);
        const FusedEstimate reached = fusedOdometry.estimate();
        fusedOdometry.reset();
        return reached;
    }

    /** The latest goal and how far it has come; safe to call from any task. */
    struct GoalStatus {
        uint32_t id = 0; // 0 before the first goal
        GoalState state = GoalState::IDLE;
        Goal goal;
        float progress = 0; // along the target's axis: m, or rad for a turn
        float timeout = 0;  // s the goal may take
    };
    GoalStatus goalStatus() const {
        std::lock_guard<std::mutex> lock(goalStatusMutex);
        return goalStatusNow;
    }

  private:
    void applyMail(const MotionInbox::Mail& mail);
    // Every mode change, from the app or a gesture, goes through here.
    void setMode(socket_message_ModesEnum mode);
    void applyMode(socket_message_ModesEnum mode);
    void startAnimation(const MotionInbox::Play &play);
    void recordAnimationStatus();
    void stopLocomotion();
    void centreSticks();
    void startGoal(const Goal& goal, uint32_t id);
    void cancelGoal();
    void driveGoal(const BodyStep& step, float dt);
    void recordGoalStatus();
    void switchVariant(KinematicsVariant variant);
    bool walkNNAllowed() const;

    std::atomic<socket_message_ModesEnum> currentMode {socket_message_ModesEnum_DEACTIVATED};
    std::atomic<socket_message_WalkGaits> currentGait {socket_message_WalkGaits_TROT};

    const KinConfig* config = nullptr;
    std::optional<Kinematics> kinematics;
    bool modeApplied = false;
    std::optional<KinematicsVariant> variantApplied;

    CommandMsg command = {0, 0, 0, 0, 0, 0, 0};
    int64_t commandRxUs = 0;
    bool linkLostNow = false;
    FallDetector fallDetector;

    friend class MotionState;

    MotionState* state = nullptr;

    RestState restState;
    AnimateState animateState;
    std::shared_ptr<const anim::Clip> sitClip;
    // The mode a clip hands back to when it ends: the one it started from, stand instead of walk, or one asked for
    // while it played.
    socket_message_ModesEnum returnMode = socket_message_ModesEnum_STAND;
    std::mutex animationStatusMutex;
    AnimationStatus animationStatusNow {"", anim::State::IDLE, 0, 0};
    StandState standState;
    WalkState walkState;
#if WALK_NN_POLICY
    WalkNNState walkNNState;
#endif
    std::atomic<bool> walkNNVerified {false};
    uint32_t lastImuValid = 0;

    body_state_t body_state;

    mutable std::mutex odometryMutex;
    FusedOdometry fusedOdometry;

    GoalController goal;
    uint32_t goalId = 0;
    mutable std::mutex goalStatusMutex;
    GoalStatus goalStatusNow;

    float new_angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    float angles[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    int64_t lastUpdate = esp_timer_get_time();
};

#endif
