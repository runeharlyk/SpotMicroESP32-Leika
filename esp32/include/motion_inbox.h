#pragma once

#include <animation/animation.h>
#include <goal_controller.h>
#include <message_types.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

/** A client of one link: client ids repeat across links. A clip or a goal without one, a gesture's, has no link. */
struct LinkClient {
    const void *link = nullptr;
    int id = -1;
    bool operator==(const LinkClient &other) const { return link == other.link && id == other.id; }
};

/**
 * What the socket's task hands the control task, so that only the control task touches the motion
 * state: the newest controller input, mode, gait, variant and goal, and whether the controller fell silent.
 * The app re-sends its input while a stick is off centre; when that stops for LINK_TIMEOUT_MS, the
 * link is lost and the robot must stop walking.
 */
class MotionInbox {
  public:
    static constexpr uint32_t LINK_TIMEOUT_MS = 500;
    // A client waiting on a goal asks for its status every half second; one silent this long has gone, whether or not
    // its link said so: a USB serial port the host closed is not reported while the host still polls the bus.
    static constexpr uint32_t OWNER_SILENCE_MS = 2000;

    /** A clip to play, loaded and validated on the socket's task, with the app's parameter values. */
    struct Play {
        std::shared_ptr<const anim::Clip> clip;
        anim::ParamValue params[anim::PARAM_MAX];
        int paramCount;
    };

    struct Mail {
        std::optional<CommandMsg> input;
        std::optional<socket_message_ModesEnum> mode;
        std::optional<socket_message_WalkGaits> gait;
        std::optional<socket_message_KinematicsVariant> variant;
        bool linkLost = false;
        int64_t inputAtUs = 0; // when the input arrived, in esp_timer microseconds; 0 without input
        std::optional<Play> play;
        std::optional<anim::Pose> pose;
        bool stopAnimation = false;
        std::optional<Goal> goal = std::nullopt;
        uint32_t goalId = 0; // the goal's number, counted from 1; 0 without one
        bool cancelGoal = false;
    };

    void postInput(const socket_message_ControllerData &data, uint32_t nowMs, int64_t nowUs) {
        CommandMsg command;
        command.fromProto(data);
        std::lock_guard<std::mutex> lock(_mutex);
        _input = command;
        _lastInputAt = nowMs;
        _inputAtUs = nowUs;
        _steering = command.steering();
    }

    void postMode(socket_message_ModesEnum mode) {
        std::lock_guard<std::mutex> lock(_mutex);
        _mode = mode;
    }

    void postGait(socket_message_WalkGaits gait) {
        std::lock_guard<std::mutex> lock(_mutex);
        _gait = gait;
    }

    void postVariant(socket_message_KinematicsVariant variant) {
        std::lock_guard<std::mutex> lock(_mutex);
        _variant = variant;
    }

    /** `owner` is the client that asked, whose leaving stops the clip. */
    void postPlay(const Play &play, LinkClient owner = {}) {
        std::lock_guard<std::mutex> lock(_mutex);
        _play = play;
        _stopAnimation = false;
        _clipOwner = owner;
    }

    /** A pose to hold; `owner` is the client that sent it, whose leaving eases it home, as for a clip. */
    void postPose(const anim::Pose &pose, LinkClient owner = {}) {
        std::lock_guard<std::mutex> lock(_mutex);
        _pose = pose;
        _stopAnimation = false;
        _clipOwner = owner;
    }

    void postStopAnimation() {
        std::lock_guard<std::mutex> lock(_mutex);
        stopAnimationLocked();
    }

    /** A goal no client owns, a gesture's; returns its number. */
    uint32_t postGoal(const Goal &goal) { return postGoal(goal, {}, 0); }

    /**
     * Returns the goal's number. `owner` is the client that asked, at `nowMs`, whose leaving or silence for
     * OWNER_SILENCE_MS cancels the goal.
     */
    uint32_t postGoal(const Goal &goal, LinkClient owner, uint32_t nowMs) {
        std::lock_guard<std::mutex> lock(_mutex);
        _goal = goal;
        _goalId = ++_goalsPosted;
        _cancelGoal = false;
        _goalOwner = owner;
        _goalOwnerHeardAt = nowMs;
        return _goalId;
    }

    /** A client sent a frame at `nowMs`: the goal it owns walks on. */
    void heard(LinkClient client, uint32_t nowMs) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (client.link && client == _goalOwner) _goalOwnerHeardAt = nowMs;
    }

    void postGoalCancel() {
        std::lock_guard<std::mutex> lock(_mutex);
        cancelGoalLocked();
    }

    /**
     * A client left: a clip it started stops through its exit, and a pose it sent eases home, as a stop request would,
     * since nobody else can stop a loop or knows the robot holds a pose;
     * a goal it started is cancelled, since nobody else may be watching it walk.
     */
    void dropClient(LinkClient client) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!client.link) return;
        if (client == _clipOwner) stopAnimationLocked();
        if (client == _goalOwner) cancelGoalLocked();
    }

    /** Everything posted since the last call; reports a lost link once per silence, ends a silent owner's goal. */
    Mail take(uint32_t nowMs) {
        std::lock_guard<std::mutex> lock(_mutex);
        Mail mail {_input, _mode, _gait, _variant};
        mail.inputAtUs = _input ? _inputAtUs : 0;
        mail.play = std::move(_play);
        mail.pose = std::exchange(_pose, std::nullopt);
        mail.stopAnimation = _stopAnimation;
        _input.reset();
        _mode.reset();
        _gait.reset();
        _variant.reset();
        _play.reset();
        _stopAnimation = false;
        if (_goalOwner.link && silentFor(nowMs, _goalOwnerHeardAt) > OWNER_SILENCE_MS) cancelGoalLocked();
        mail.goal = std::exchange(_goal, std::nullopt);
        mail.goalId = mail.goal ? _goalId : 0;
        mail.cancelGoal = std::exchange(_cancelGoal, false);
        if (_steering && silentFor(nowMs, _lastInputAt) > LINK_TIMEOUT_MS) {
            mail.linkLost = true;
            _steering = false;
        }
        return mail;
    }

  private:
    // A poster reads its clock before it takes the lock, and so does the taker: an input or a frame posted after the
    // take read its clock carries a later time, which as an unsigned difference would be a silence of 49 days.
    static int64_t silentFor(uint32_t nowMs, uint32_t heardMs) { return static_cast<int32_t>(nowMs - heardMs); }

    void stopAnimationLocked() {
        _play.reset();
        _pose.reset();
        _stopAnimation = true;
        _clipOwner = {};
    }

    void cancelGoalLocked() {
        _goal.reset();
        _cancelGoal = true;
        _goalOwner = {};
    }

    std::mutex _mutex;
    std::optional<CommandMsg> _input;
    std::optional<socket_message_ModesEnum> _mode;
    std::optional<socket_message_WalkGaits> _gait;
    std::optional<socket_message_KinematicsVariant> _variant;
    std::optional<Play> _play;
    std::optional<anim::Pose> _pose;
    bool _stopAnimation = false;
    LinkClient _clipOwner;
    uint32_t _lastInputAt = 0;
    int64_t _inputAtUs = 0;
    bool _steering = false;
    std::optional<Goal> _goal;
    uint32_t _goalId = 0, _goalsPosted = 0;
    bool _cancelGoal = false;
    LinkClient _goalOwner;
    uint32_t _goalOwnerHeardAt = 0;
};
