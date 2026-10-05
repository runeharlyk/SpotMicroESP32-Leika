#pragma once

#include <animation/animation.h>
#include <motion_states/state.h>
#include <memory>
#include <optional>

/**
 * Plays an animation clip. The player drives the body and the feet, which the body state follows so the state that
 * takes over next starts from where the clip left the legs; a leg with joint targets sets its angles directly, so
 * angles() replaces the IK of the body state while this state runs.
 */
class AnimateState : public MotionState {
  protected:
    const char *name() const override { return "Animate"; }

  public:
    void configure(const KinConfig &config) override {
        MotionState::configure(config);
        kin_.emplace(config);
        player_.emplace(config, *kin_);
    }

    /** Plays `clip` from the live legs on the next step, with the app's values for its parameters. */
    void start(std::shared_ptr<const anim::Clip> clip, const anim::ParamValue *params, int count) {
        pending_ = std::move(clip);
        paramCount_ = count < anim::PARAM_MAX ? count : anim::PARAM_MAX;
        for (int i = 0; i < paramCount_; ++i) params_[i] = params[i];
    }

    /** Leaves the clip through its exit blend back to stance. */
    void stop() {
        if (pending_) pending_.reset();
        player_->stop();
    }

    /** Drops the clip at once, for a robot deactivated while it plays. */
    void abandon() {
        pending_.reset();
        player_.emplace(*kin, *kin_);
    }

    /** Whether the clip has played out and the legs are back in stance. */
    bool finished() const { return !pending_ && player_->state() == anim::State::IDLE; }

    void step(body_state_t &body_state, float dt) override {
        const float (*stance)[4] = kin->default_feet_positions;
        if (pending_) {
            anim::Pose live;
            anim::capturePose(body_state, stance, body_state.ym, live);
            clip_ = std::move(pending_);
            player_->play(clip_.get(), params_, paramCount_, live, body_state.ym);
        }
        const anim::Pose &pose = player_->update(dt);
        const float base = player_->base();
        anim::toBodyState(pose.body, stance, base, body_state);
        for (int leg = 0; leg < anim::LEGS; ++leg)
            if (!pose.legs[leg].joints) anim::addFootOffset(body_state, leg, pose.legs[leg].v);
        clampedMask_ = anim::poseToAngles(pose, *kin_, stance, base, angles_);
    }

    const float *angles() const { return angles_; }

    struct Status {
        const char *name;
        anim::State state;
        float t;
        uint32_t clampedMask;
    };

    Status status() const {
        return {clip_ ? clip_->name : "", player_ ? player_->state() : anim::State::IDLE, player_ ? player_->t() : 0.0f,
                clampedMask_};
    }

  private:
    std::optional<Kinematics> kin_;
    std::optional<anim::Player> player_;
    std::shared_ptr<const anim::Clip> pending_;
    std::shared_ptr<const anim::Clip> clip_;
    anim::ParamValue params_[anim::PARAM_MAX] = {};
    int paramCount_ = 0;
    float angles_[anim::JOINTS] = {};
    uint32_t clampedMask_ = 0;
};
