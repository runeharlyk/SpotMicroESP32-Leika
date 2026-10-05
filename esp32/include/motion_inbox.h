#pragma once

#include <animation/animation.h>
#include <message_types.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

/**
 * What the socket's task hands the control task, so that only the control task touches the motion
 * state: the newest controller input, mode, gait and variant, and whether the controller fell silent.
 * The app re-sends its input while a stick is off centre; when that stops for LINK_TIMEOUT_MS, the
 * link is lost and the robot must stop walking.
 */
class MotionInbox {
  public:
    static constexpr uint32_t LINK_TIMEOUT_MS = 500;

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
        bool stopAnimation = false;
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

    void postPlay(const Play &play) {
        std::lock_guard<std::mutex> lock(_mutex);
        _play = play;
        _stopAnimation = false;
    }

    void postStopAnimation() {
        std::lock_guard<std::mutex> lock(_mutex);
        _play.reset();
        _stopAnimation = true;
    }

    /** Everything posted since the last call; reports a lost link once per silence. */
    Mail take(uint32_t nowMs) {
        std::lock_guard<std::mutex> lock(_mutex);
        Mail mail {_input, _mode, _gait, _variant};
        mail.inputAtUs = _input ? _inputAtUs : 0;
        mail.play = std::move(_play);
        mail.stopAnimation = _stopAnimation;
        _input.reset();
        _mode.reset();
        _gait.reset();
        _variant.reset();
        _play.reset();
        _stopAnimation = false;
        if (_steering && nowMs - _lastInputAt > LINK_TIMEOUT_MS) {
            mail.linkLost = true;
            _steering = false;
        }
        return mail;
    }

  private:
    std::mutex _mutex;
    std::optional<CommandMsg> _input;
    std::optional<socket_message_ModesEnum> _mode;
    std::optional<socket_message_WalkGaits> _gait;
    std::optional<socket_message_KinematicsVariant> _variant;
    std::optional<Play> _play;
    bool _stopAnimation = false;
    uint32_t _lastInputAt = 0;
    int64_t _inputAtUs = 0;
    bool _steering = false;
};
