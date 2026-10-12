#pragma once

#include <goal_controller.h>
#include <message_types.h>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

/**
 * What the socket's task hands the control task, so that only the control task touches the motion
 * state: the newest controller input, mode, gait, variant and goal, and whether the controller fell silent.
 * The app re-sends its input while a stick is off centre; when that stops for LINK_TIMEOUT_MS, the
 * link is lost and the robot must stop walking.
 */
class MotionInbox {
  public:
    static constexpr uint32_t LINK_TIMEOUT_MS = 500;

    struct Mail {
        std::optional<CommandMsg> input;
        std::optional<socket_message_ModesEnum> mode;
        std::optional<socket_message_WalkGaits> gait;
        std::optional<socket_message_KinematicsVariant> variant;
        bool linkLost = false;
        int64_t inputAtUs = 0; // when the input arrived, in esp_timer microseconds; 0 without input
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

    /**
     * Returns the goal's number. `owner` is the socket client that asked, whose leaving cancels the goal; -1 for a
     * link that never leaves.
     */
    uint32_t postGoal(const Goal &goal, int owner = -1) {
        std::lock_guard<std::mutex> lock(_mutex);
        _goal = goal;
        _goalId = ++_goalsPosted;
        _cancelGoal = false;
        _goalOwner = owner;
        return _goalId;
    }

    void postGoalCancel() {
        std::lock_guard<std::mutex> lock(_mutex);
        cancelGoalLocked();
    }

    /** A client left: a goal it started is cancelled, since nobody else may be watching it walk. */
    void dropClient(int client) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (client >= 0 && client == _goalOwner) cancelGoalLocked();
    }

    /** Everything posted since the last call; reports a lost link once per silence. */
    Mail take(uint32_t nowMs) {
        std::lock_guard<std::mutex> lock(_mutex);
        Mail mail {_input, _mode, _gait, _variant};
        mail.inputAtUs = _input ? _inputAtUs : 0;
        _input.reset();
        _mode.reset();
        _gait.reset();
        _variant.reset();
        mail.goal = std::exchange(_goal, std::nullopt);
        mail.goalId = mail.goal ? _goalId : 0;
        mail.cancelGoal = std::exchange(_cancelGoal, false);
        if (_steering && nowMs - _lastInputAt > LINK_TIMEOUT_MS) {
            mail.linkLost = true;
            _steering = false;
        }
        return mail;
    }

  private:
    void cancelGoalLocked() {
        _goal.reset();
        _cancelGoal = true;
        _goalOwner = -1;
    }

    std::mutex _mutex;
    std::optional<CommandMsg> _input;
    std::optional<socket_message_ModesEnum> _mode;
    std::optional<socket_message_WalkGaits> _gait;
    std::optional<socket_message_KinematicsVariant> _variant;
    uint32_t _lastInputAt = 0;
    int64_t _inputAtUs = 0;
    bool _steering = false;
    std::optional<Goal> _goal;
    uint32_t _goalId = 0, _goalsPosted = 0;
    bool _cancelGoal = false;
    int _goalOwner = -1;
};
