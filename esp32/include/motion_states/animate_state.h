#pragma once

#include <animation/animation.h>
#include <motion_states/state.h>
#include <memory>
#include <optional>

/**
 * Plays an animation clip, or holds a pose an app shows on the robot. The player drives the body and the feet, which
 * the body state follows so the state that takes over next starts from where the clip left the legs; a leg with joint
 * targets sets its angles directly, so angles() replaces the IK of the body state while this state runs.
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

    /**
     * Holds `pose`, which the legs ease to from where they are, each channel as the stand's body follows its target,
     * and keep while no other comes; refused while a clip plays, and once stop() eases them back to stance, so a mode
     * asked for meanwhile wins over a pose still on its way.
     */
    bool hold(const anim::Pose &pose) {
        if (pending_ || homing_ || player_->state() != anim::State::IDLE) return false;
        if (!holding_) startHold_ = true;
        holding_ = true;
        target_ = pose;
        return true;
    }

    /** Leaves the clip through its exit blend, or a held pose by easing it, back to stance. */
    void stop() {
        if (pending_) pending_.reset();
        player_->stop();
        homing_ = holding_;
        target_ = anim::Pose {};
    }

    /** Drops the clip or the pose at once, for a robot deactivated meanwhile. */
    void abandon() {
        pending_.reset();
        player_.emplace(*kin, *kin_);
        holding_ = homing_ = false;
    }

    /** Whether the clip has played out, or the pose eased home, and the legs are back in stance. */
    bool finished() const { return !pending_ && !holding_ && player_->state() == anim::State::IDLE; }

    void step(body_state_t &body_state, float dt) override {
        const float (*stance)[4] = kin->default_feet_positions;
        if (holding_ && startHold_) {
            anim::capturePose(body_state, stance, body_state.ym, held_);
            holdBase_ = body_state.ym;
            holdDampers_ = {};
            startHold_ = false;
        }
        if (pending_) {
            anim::Pose live;
            float liveBase = body_state.ym;
            if (holding_) {
                live = held_;
                liveBase = holdBase_;
                holding_ = homing_ = false;
            } else {
                anim::capturePose(body_state, stance, body_state.ym, live);
            }
            clip_ = std::move(pending_);
            player_->play(clip_.get(), params_, paramCount_, live, liveBase);
        }
        if (holding_) followHeld(dt);
        const anim::Pose &pose = holding_ ? held_ : player_->update(dt);
        const float base = holding_ ? holdBase_ : player_->base();
        if (homing_ && atStance(held_)) holding_ = homing_ = false;
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
    // A held joint leg whose target is a foot goes back to the foot once within SETTLED_DEG of the foot's joints, with
    // the body within HOME_BODY of its target; a pose is at stance with only feet, within HOME_FOOT_MM of their
    // standing spots, and the body within HOME_BODY (mm or rad) of zero.
    static constexpr float SETTLED_DEG = 0.01f, HOME_FOOT_MM = 1.0f, HOME_BODY = 0.01f;

    struct HoldDampers {
        CriticalDamper body[6];
        CriticalDamper legs[anim::LEGS][3];
    };

    static bool atStance(const anim::Pose &pose) {
        for (float v : pose.body)
            if (std::fabs(v) > HOME_BODY) return false;
        for (const anim::LegTarget &leg : pose.legs) {
            if (leg.joints) return false;
            for (float v : leg.v)
                if (std::fabs(v) > HOME_FOOT_MM) return false;
        }
        return true;
    }

    // The held pose one tick on toward the target. A leg that is a joint leg on either side moves in joint space, its
    // foot side solved on that side's body; it stays a joint leg until it has settled on a foot target, where the two
    // agree, so changing between a foot and joints never jumps.
    void followHeld(float dt) {
        const float (*stance)[4] = kin->default_feet_positions;
        bool bodySettled = true;
        for (int a = 0; a < 6; ++a) {
            follow(holdDampers_.body[a], held_.body[a], target_.body[a], dt);
            bodySettled &= std::fabs(held_.body[a] - target_.body[a]) <= HOME_BODY;
        }
        for (int leg = 0; leg < anim::LEGS; ++leg) {
            anim::LegTarget &at = held_.legs[leg];
            const anim::LegTarget &to = target_.legs[leg];
            float goal[3] = {to.v[0], to.v[1], to.v[2]};
            if (!to.joints && at.joints) anim::legJointsDeg(*kin_, target_.body, to.v, leg, stance, holdBase_, goal);
            if (to.joints && !at.joints) {
                anim::legJointsDeg(*kin_, held_.body, at.v, leg, stance, holdBase_, at.v);
                at.joints = true;
                for (CriticalDamper &damper : holdDampers_.legs[leg]) damper = {};
            }
            bool legSettled = true;
            for (int k = 0; k < 3; ++k) {
                follow(holdDampers_.legs[leg][k], at.v[k], goal[k], dt);
                legSettled &= std::fabs(at.v[k] - goal[k]) <= SETTLED_DEG;
            }
            if (at.joints && !to.joints && legSettled && bodySettled) {
                at = to;
                for (CriticalDamper &damper : holdDampers_.legs[leg]) damper = {};
            }
        }
    }

    std::optional<Kinematics> kin_;
    std::optional<anim::Player> player_;
    std::shared_ptr<const anim::Clip> pending_;
    std::shared_ptr<const anim::Clip> clip_;
    anim::ParamValue params_[anim::PARAM_MAX] = {};
    int paramCount_ = 0;
    float angles_[anim::JOINTS] = {};
    uint32_t clampedMask_ = 0;
    bool holding_ = false, startHold_ = false, homing_ = false;
    anim::Pose held_, target_;
    float holdBase_ = 0;
    HoldDampers holdDampers_;
};
