#pragma once

// Animation clips, ported from the Hexapod (firmware/include/animation/animation.h there) to four legs: the clip
// model, its validator, the evaluator, the pose-to-servo-angle path and the player. This file is the reference for
// the app's TypeScript port, which is pinned to fixtures exported from it. It knows nothing about nanopb or the ESP.
//
// Clip units and frame: lengths mm, body angles rad, joint angles deg in the IK output convention (before
// MotionService's JOINT_DIRECTION), time s, all float32. Offsets are REP-103: x forward, y left, z up; positive roll
// lifts the left side, positive pitch lowers the nose, positive yaw turns left. toBodyState() maps them onto Leika's
// body state (metres and degrees; x forward, y up, z left).

#include <cmath>
#include <cstdint>
#include <cstring>
#include <kinematics.h>

namespace anim {

constexpr int LEGS = 4;
constexpr int JOINTS = LEGS * 3;
constexpr int KEYFRAME_MAX = 32;
constexpr int OVERLAY_MAX = 8;
constexpr int PARAM_MAX = 10;
constexpr int NAME_LEN_MAX = 32;
constexpr int DESCRIPTION_LEN_MAX = 96;
constexpr uint32_t SCHEMA_VERSION = 1;
constexpr float DEFAULT_ENTRY_S = 0.5f;
constexpr float DEFAULT_EXIT_S = 0.5f;
// A foot travelling during Entry or Exit lifts on an arc, as a step does; its height is this share of the variant's
// leg reach, reached for travels of STEP_ARC_FULL_TRAVEL_MM and more.
constexpr float STEP_ARC_REACH_SHARE = 0.25f;
constexpr float STEP_ARC_FULL_TRAVEL_MM = 40.0f;
constexpr float STEP_ARC_MIN_TRAVEL_MM = 2.0f;

enum Ease : int { LINEAR = 0, EASE_IN = 1, EASE_OUT = 2, EASE_IN_OUT = 3 };

enum ParamId : int {
    SPEED = 0,
    BODY_X = 1,
    BODY_Y = 2,
    BODY_Z = 3,
    BODY_ROLL = 4,
    BODY_PITCH = 5,
    BODY_YAW = 6,
    FOOT_LIFT = 7,
    OVERLAY_AMPLITUDE = 8,
    REPEAT = 9,
    PARAM_COUNT = 10
};

enum ChannelKind : int { CHANNEL_NONE = 0, CHANNEL_BODY = 1, CHANNEL_FOOT = 2 };

enum BodyAxis : int { ROLL = 0, PITCH = 1, YAW = 2, X = 3, Y = 4, Z = 5 };

constexpr ParamId BODY_PARAM_FOR_AXIS[6] = {BODY_ROLL, BODY_PITCH, BODY_YAW, BODY_X, BODY_Y, BODY_Z};

// A leg is either a foot offset from its standing foot (mm, +z lifts) or three joint angles (deg).
struct LegTarget {
    bool joints = false;
    float v[3] = {0.0f, 0.0f, 0.0f};
};

struct Keyframe {
    float time = 0.0f;
    int ease = LINEAR;
    float body[6] = {0, 0, 0, 0, 0, 0};  // roll, pitch, yaw, x, y, z offsets
    int legCount = 0;                    // 0 = every foot holds stance, else LEGS
    LegTarget legs[LEGS];
};

struct Overlay {
    ChannelKind kind = CHANNEL_NONE;  // channel is a BodyAxis for CHANNEL_BODY, else leg * 3 + axis
    int channel = 0;
    float amplitude = 0.0f;
    float frequency = 1.0f;
    float phase = 0.0f;
    float start = 0.0f;
    float end = 0.0f;
};

struct ParamSpec {
    int id = 0;
    float min = 0.0f;
    float defaultValue = 1.0f;
    float max = 1.0f;
};

struct ParamValue {
    int id;
    float value;
};

struct Clip {
    char name[NAME_LEN_MAX + 1] = {0};
    char description[DESCRIPTION_LEN_MAX + 1] = {0};
    uint32_t schema = SCHEMA_VERSION;
    bool loop = false;
    bool holdEnd = false;
    float entryTime = 0.0f;
    float exitTime = 0.0f;
    int keyframeCount = 0;
    Keyframe keyframes[KEYFRAME_MAX];
    int overlayCount = 0;
    Overlay overlays[OVERLAY_MAX];
    int paramCount = 0;
    ParamSpec params[PARAM_MAX];
    bool hasRideHeight = false;  // else the body stays at the height it had when the clip started
    float rideHeight = 0.0f;     // mm, the body's height above the feet while playing

    float duration() const { return keyframeCount > 0 ? keyframes[keyframeCount - 1].time : 0.0f; }
    float entrySeconds() const { return entryTime > 0.0f ? entryTime : DEFAULT_ENTRY_S; }
    float exitSeconds() const { return exitTime > 0.0f ? exitTime : DEFAULT_EXIT_S; }
};

inline LegTarget legTarget(const Keyframe &k, int leg) { return k.legCount > 0 ? k.legs[leg] : LegTarget {}; }

inline bool validName(const char *name) {
    const size_t n = strlen(name);
    if (n < 1 || n > NAME_LEN_MAX) return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = name[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// The first structural error as a static string, or nullptr. Finite checks use the bit test, since -Ofast folds
// std::isfinite.
inline bool finite(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

inline const char *validate(const Clip &c) {
    if (c.schema != SCHEMA_VERSION) return "schema is not 1";
    if (!validName(c.name)) return "name must be 1-32 characters of [a-z0-9_-]";
    if (strlen(c.description) > (size_t)DESCRIPTION_LEN_MAX) return "description longer than 96 bytes";
    if (c.loop && c.holdEnd) return "loop and hold_end cannot both be set";
    if (c.keyframeCount < 1) return "at least one keyframe is required";
    if (c.keyframeCount > KEYFRAME_MAX) return "more than 32 keyframes";
    if (c.overlayCount > OVERLAY_MAX) return "more than 8 overlays";
    if (c.paramCount > PARAM_MAX) return "more than 10 params";
    for (int i = 0; i < c.keyframeCount; ++i) {
        const Keyframe &k = c.keyframes[i];
        if (k.legCount != 0 && k.legCount != LEGS) return "keyframe must have 0 or 4 legs";
        if (k.ease < LINEAR || k.ease > EASE_IN_OUT) return "keyframe ease out of range";
    }
    if (!finite(c.entryTime) || !finite(c.exitTime)) return "entry/exit time must be finite";
    if (c.hasRideHeight && (!finite(c.rideHeight) || c.rideHeight <= 0.0f)) return "ride_height must be positive";
    for (int i = 0; i < c.keyframeCount; ++i) {
        const Keyframe &k = c.keyframes[i];
        if (!finite(k.time)) return "keyframe time must be finite";
        for (float v : k.body)
            if (!finite(v)) return "keyframe body must be finite";
        for (int l = 0; l < k.legCount; ++l)
            for (float v : k.legs[l].v)
                if (!finite(v)) return "keyframe leg must be finite";
    }
    for (int i = 0; i < c.overlayCount; ++i) {
        const Overlay &o = c.overlays[i];
        if (!finite(o.amplitude) || !finite(o.frequency) || !finite(o.phase) || !finite(o.start) || !finite(o.end))
            return "overlay must be finite";
    }
    for (int i = 0; i < c.paramCount; ++i) {
        const ParamSpec &p = c.params[i];
        if (!finite(p.min) || !finite(p.defaultValue) || !finite(p.max)) return "param must be finite";
    }
    if (c.keyframes[0].time != 0.0f) return "first keyframe must be at time 0";
    for (int i = 1; i < c.keyframeCount; ++i)
        if (c.keyframes[i].time <= c.keyframes[i - 1].time) return "keyframe time must increase";
    for (int i = 0; i < c.overlayCount; ++i) {
        const Overlay &o = c.overlays[i];
        if (o.kind == CHANNEL_NONE) return "overlay needs exactly one channel";
        if (o.kind == CHANNEL_BODY && (o.channel < 0 || o.channel > 5)) return "overlay body_axis out of range";
        if (o.kind == CHANNEL_FOOT && (o.channel < 0 || o.channel >= JOINTS)) return "overlay foot_channel out of range";
        if (o.start < 0.0f || o.start >= o.end) return "overlay window must have 0 <= start < end";
        if (o.end > c.duration()) return "overlay end is after the last keyframe";
    }
    for (int i = 0; i < c.paramCount; ++i) {
        const ParamSpec &p = c.params[i];
        if (p.id < 0 || p.id >= PARAM_COUNT) return "param id out of range";
        for (int j = 0; j < i; ++j)
            if (c.params[j].id == p.id) return "param id is not unique";
        if (!(p.min <= p.defaultValue && p.defaultValue <= p.max)) return "param needs min <= default_value <= max";
        if (p.id == SPEED && p.min <= 0.0f) return "param SPEED needs a positive min";
        if (p.id == REPEAT && p.min < 1.0f) return "param REPEAT needs min >= 1";
    }
    return nullptr;
}

struct Pose {
    float body[6] = {0, 0, 0, 0, 0, 0};
    LegTarget legs[LEGS];
};

inline float easeValue(int kind, float t) {
    switch (kind) {
        case EASE_IN: return t * t;
        case EASE_OUT: return t * (2.0f - t);
        case EASE_IN_OUT: return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
        default: return t;
    }
}

// Every id gets a value: a declared id takes the caller's value clamped to its range, else its default; an undeclared
// id is 1 (the neutral multiplier and a single play).
inline void resolveParams(const Clip &c, const ParamValue *values, int count, float out[PARAM_COUNT]) {
    for (int i = 0; i < PARAM_COUNT; ++i) out[i] = 1.0f;
    for (int i = 0; i < c.paramCount; ++i) {
        const ParamSpec &spec = c.params[i];
        float v = spec.defaultValue;
        for (int j = 0; j < count; ++j)
            if (values[j].id == spec.id) v = values[j].value;
        out[spec.id] = fminf(fmaxf(v, spec.min), spec.max);
    }
}

// Leika's body state is left-handed (x forward, y up, z left): omega turns about x, phi about the vertical, psi about
// the lateral axis, each the opposite way to REP-103. Measured on the Pico for roll and pitch, and pinned for all three
// by the geometry checks in test/host/animation_test.cpp.
constexpr float ROLL_TO_OMEGA = -1.0f;
constexpr float PITCH_TO_PSI = -1.0f;
constexpr float YAW_TO_PHI = -1.0f;
constexpr float MM = 0.001f;

// The body state for clip offsets body6 on the standing feet, with the body baseHeight (m) above them.
inline void toBodyState(const float body6[6], const float stance[LEGS][4], float baseHeight, body_state_t &b) {
    b.omega = ROLL_TO_OMEGA * RAD_TO_DEG_F(body6[ROLL]);
    b.psi = PITCH_TO_PSI * RAD_TO_DEG_F(body6[PITCH]);
    b.phi = YAW_TO_PHI * RAD_TO_DEG_F(body6[YAW]);
    b.xm = body6[X] * MM;
    b.zm = body6[Y] * MM;
    b.ym = baseHeight + body6[Z] * MM;
    b.updateFeet(stance);
}

inline void addFootOffset(body_state_t &b, int leg, const float offset[3]) {
    b.feet[leg][0] += offset[0] * MM;
    b.feet[leg][2] += offset[1] * MM;
    b.feet[leg][1] += offset[2] * MM;
}

// The clip pose of a live body state: the inverse of toBodyState() and addFootOffset().
inline void capturePose(const body_state_t &b, const float stance[LEGS][4], float baseHeight, Pose &out) {
    out.body[ROLL] = DEG_TO_RAD_F(b.omega) / ROLL_TO_OMEGA;
    out.body[PITCH] = DEG_TO_RAD_F(b.psi) / PITCH_TO_PSI;
    out.body[YAW] = DEG_TO_RAD_F(b.phi) / YAW_TO_PHI;
    out.body[X] = b.xm / MM;
    out.body[Y] = b.zm / MM;
    out.body[Z] = (b.ym - baseHeight) / MM;
    for (int i = 0; i < LEGS; ++i) {
        out.legs[i].joints = false;
        out.legs[i].v[0] = (b.feet[i][0] - stance[i][0]) / MM;
        out.legs[i].v[1] = (b.feet[i][2] - stance[i][2]) / MM;
        out.legs[i].v[2] = (b.feet[i][1] - stance[i][1]) / MM;
    }
}

// The joints of one foot leg, solved on the body the runner outputs, so a joint leg meets its foot endpoint.
inline void legJointsDeg(Kinematics &kin, const float body6[6], const float foot[3], int leg,
                         const float stance[LEGS][4], float baseHeight, float out[3]) {
    body_state_t b;
    toBodyState(body6, stance, baseHeight, b);
    addFootOffset(b, leg, foot);
    float angles[JOINTS];
    kin.calculate_inverse_kinematics(b, angles);
    for (int k = 0; k < 3; ++k) out[k] = angles[leg * 3 + k];
}

// The keyframe pair bracketing t and the eased fraction between them, with t clamped.
inline void segment(const Clip &c, float t, const Keyframe *&k0, const Keyframe *&k1, float &u) {
    if (t <= 0.0f || c.keyframeCount == 1) {
        k0 = k1 = &c.keyframes[0];
        u = 0.0f;
        return;
    }
    if (t >= c.keyframes[c.keyframeCount - 1].time) {
        k0 = k1 = &c.keyframes[c.keyframeCount - 1];
        u = 0.0f;
        return;
    }
    int i = 1;
    while (c.keyframes[i].time < t) ++i;
    k0 = &c.keyframes[i - 1];
    k1 = &c.keyframes[i];
    u = easeValue(k1->ease, (t - k0->time) / (k1->time - k0->time));
}

// Interpolate the body; add body overlays and collect each foot overlay; apply the BODY_* multipliers (this is the
// output body); then resolve the legs. A foot leg is lerped and gets its overlay and FOOT_LIFT on z. A joint leg is
// lerped raw. A mixed leg takes the foot endpoint with its overlay and lift, solves it against the output body, and
// lerps in joint space, so the servo command is continuous at the keyframe where the leg switches.
inline void evaluate(const Clip &c, const float params[PARAM_COUNT], float t, Kinematics &kin,
                     const float stance[LEGS][4], float baseHeight, Pose &out) {
    const Keyframe *k0;
    const Keyframe *k1;
    float u;
    segment(c, t, k0, k1, u);
    t = fminf(fmaxf(t, 0.0f), c.duration());
    float body[6];
    for (int a = 0; a < 6; ++a) body[a] = k0->body[a] + (k1->body[a] - k0->body[a]) * u;
    float footOverlay[LEGS][3] = {};
    for (int i = 0; i < c.overlayCount; ++i) {
        const Overlay &o = c.overlays[i];
        if (!(o.start <= t && t <= o.end)) continue;
        const float v = o.amplitude * params[OVERLAY_AMPLITUDE] * sinf(2.0f * (float)M_PI * o.frequency * t + o.phase);
        if (o.kind == CHANNEL_BODY) body[o.channel] += v;
        else footOverlay[o.channel / 3][o.channel % 3] += v;
    }
    for (int a = 0; a < 6; ++a) body[a] *= params[BODY_PARAM_FOR_AXIS[a]];
    for (int a = 0; a < 6; ++a) out.body[a] = body[a];

    for (int i = 0; i < LEGS; ++i) {
        const LegTarget a = legTarget(*k0, i);
        const LegTarget b = legTarget(*k1, i);
        LegTarget &leg = out.legs[i];
        auto lifted = [&](const float foot[3], float dst[3]) {
            for (int k = 0; k < 3; ++k) dst[k] = foot[k] + footOverlay[i][k];
            dst[2] *= params[FOOT_LIFT];
        };
        if (!a.joints && !b.joints) {
            float lerp[3];
            for (int k = 0; k < 3; ++k) lerp[k] = a.v[k] + (b.v[k] - a.v[k]) * u;
            leg.joints = false;
            lifted(lerp, leg.v);
        } else if (a.joints && b.joints) {
            leg.joints = true;
            for (int k = 0; k < 3; ++k) leg.v[k] = a.v[k] + (b.v[k] - a.v[k]) * u;
        } else {
            float ja[3], jb[3], foot[3];
            if (a.joints) {
                for (int k = 0; k < 3; ++k) ja[k] = a.v[k];
            } else {
                lifted(a.v, foot);
                legJointsDeg(kin, body, foot, i, stance, baseHeight, ja);
            }
            if (b.joints) {
                for (int k = 0; k < 3; ++k) jb[k] = b.v[k];
            } else {
                lifted(b.v, foot);
                legJointsDeg(kin, body, foot, i, stance, baseHeight, jb);
            }
            leg.joints = true;
            for (int k = 0; k < 3; ++k) leg.v[k] = ja[k] + (jb[k] - ja[k]) * u;
        }
    }
}

// The 12 joint angles (deg, IK order) and a 12-bit mask, bit leg * 3 + joint: the femur and tibia bits of a foot leg
// the IK could not reach, which it then bends as far as it goes.
inline uint32_t poseToAngles(const Pose &p, Kinematics &kin, const float stance[LEGS][4], float baseHeight,
                             float angles[JOINTS]) {
    body_state_t b;
    toBodyState(p.body, stance, baseHeight, b);
    for (int i = 0; i < LEGS; ++i)
        if (!p.legs[i].joints) addFootOffset(b, i, p.legs[i].v);
    uint8_t unreachable = 0;
    kin.calculate_inverse_kinematics(b, angles, &unreachable);
    uint32_t mask = 0;
    for (int i = 0; i < LEGS; ++i) {
        if (p.legs[i].joints) {
            for (int k = 0; k < 3; ++k) angles[i * 3 + k] = p.legs[i].v[k];
        } else if (unreachable & (1u << i)) {
            mask |= 0x6u << (i * 3);
        }
    }
    return mask;
}

// The clamp mask over the whole clip at its default parameters, sampled this many times per segment: a warning for the
// author, as the clip still plays.
constexpr int SWEEP_SAMPLES = 32;

inline uint32_t clampSweep(const Clip &c, Kinematics &kin, const float stance[LEGS][4], float baseHeight) {
    float params[PARAM_COUNT];
    resolveParams(c, nullptr, 0, params);
    const float height = c.hasRideHeight ? c.rideHeight * MM : baseHeight;
    uint32_t mask = 0;
    const int segments = c.keyframeCount > 1 ? c.keyframeCount - 1 : 1;
    for (int s = 0; s < segments; ++s) {
        const float t0 = c.keyframes[s].time;
        const float t1 = c.keyframeCount > 1 ? c.keyframes[s + 1].time : t0;
        for (int i = 0; i <= SWEEP_SAMPLES; ++i) {
            Pose pose;
            evaluate(c, params, t0 + (t1 - t0) * i / SWEEP_SAMPLES, kin, stance, height, pose);
            float angles[JOINTS];
            mask |= poseToAngles(pose, kin, stance, height, angles);
        }
    }
    return mask;
}

enum class State : int { IDLE = 0, ENTRY = 1, PLAYING = 2, HOLD = 3, EXIT = 4 };

// Entry -> Playing -> Hold | Exit -> Idle around evaluate(). REPEAT is max(1, floor(x + 0.5)); SPEED scales only the
// playing clock; Entry and Exit run on wall time; a play during any state starts Entry from the current pose; Exit
// after a finished play starts from the final keyframe. Poses are offsets from the stance at a base height (m): the
// clip's ride height while it plays, else the height the body had at play().
class Player {
  public:
    Player(const KinConfig &config, Kinematics &kin) : config_(config), kin_(kin) {}

    void play(const Clip *clip, const ParamValue *values, int count, const Pose &live, float baseHeight) {
        clip_ = clip;
        resolveParams(*clip, values, count, params_);
        t_ = 0.0f;
        playsDone_ = 0;
        lastPose_ = live;
        startBase_ = baseHeight;
        playBase_ = clip->hasRideHeight ? clip->rideHeight * MM : baseHeight;
        Pose first;
        evaluate(*clip, params_, 0.0f, kin_, stance(), playBase_, first);
        startBlend(live, first, clip->entrySeconds(), State::ENTRY, startBase_, playBase_);
    }

    void stop() {
        if (state_ == State::IDLE) return;
        startBlend(lastPose_, Pose {}, clip_->exitSeconds(), State::EXIT, base(), startBase_);
    }

    const Pose &update(float dt) {
        if (state_ == State::IDLE) return lastPose_;
        if (state_ == State::ENTRY || state_ == State::EXIT) advanceBlend(dt);
        else if (state_ == State::HOLD)
            evaluate(*clip_, params_, clip_->duration(), kin_, stance(), playBase_, lastPose_);
        else
            advancePlaying(dt);
        return lastPose_;
    }

    State state() const { return state_; }
    float t() const { return t_; }
    const Pose &lastPose() const { return lastPose_; }
    const Clip *clip() const { return clip_; }

    // The body's height the last pose is an offset from: moving from the start height to the clip's during Entry,
    // back during Exit, so the base arrives exactly when the blend ends.
    float base() const {
        const float u = fminf(1.0f, blendT_ / blendSeconds_);
        const float e = easeValue(EASE_IN_OUT, u);
        if (state_ == State::ENTRY) return startBase_ + (playBase_ - startBase_) * e;
        if (state_ == State::EXIT) return exitFromBase_ + (startBase_ - exitFromBase_) * e;
        if (state_ == State::IDLE) return startBase_;
        return playBase_;
    }

  private:
    const KinConfig &config_;
    Kinematics &kin_;
    State state_ = State::IDLE;
    const Clip *clip_ = nullptr;
    float params_[PARAM_COUNT] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    float t_ = 0.0f;
    Pose lastPose_;
    int playsDone_ = 0;
    float blendT_ = 0.0f;
    float blendSeconds_ = 1.0f;
    float startBase_ = 0.0f;
    float playBase_ = 0.0f;
    float exitFromBase_ = 0.0f;
    Pose blendFrom_;
    Pose blendTo_;

    const float (*stance() const)[4] { return config_.default_feet_positions; }

    // Legs where either side is a joint target are converted to joints on both sides once, so the blend itself is a
    // plain lerp; foot legs keep their offsets and get the step arc.
    void startBlend(const Pose &src, const Pose &dst, float seconds, State state, float srcBase, float dstBase) {
        if (state == State::EXIT) exitFromBase_ = srcBase;
        blendFrom_ = src;
        blendTo_ = dst;
        for (int i = 0; i < LEGS; ++i) {
            if (!blendFrom_.legs[i].joints && !blendTo_.legs[i].joints) continue;
            if (!blendFrom_.legs[i].joints) {
                float j[3];
                legJointsDeg(kin_, blendFrom_.body, blendFrom_.legs[i].v, i, stance(), srcBase, j);
                blendFrom_.legs[i] = {true, {j[0], j[1], j[2]}};
            }
            if (!blendTo_.legs[i].joints) {
                float j[3];
                legJointsDeg(kin_, blendTo_.body, blendTo_.legs[i].v, i, stance(), dstBase, j);
                blendTo_.legs[i] = {true, {j[0], j[1], j[2]}};
            }
        }
        blendSeconds_ = seconds;
        blendT_ = 0.0f;
        state_ = state;
    }

    void advanceBlend(float dt) {
        blendT_ += dt;
        const float u = fminf(1.0f, blendT_ / blendSeconds_);
        const float e = easeValue(EASE_IN_OUT, u);
        const float arc = STEP_ARC_REACH_SHARE * config_.max_leg_reach / MM;
        for (int a = 0; a < 6; ++a)
            lastPose_.body[a] = blendFrom_.body[a] + (blendTo_.body[a] - blendFrom_.body[a]) * e;
        for (int i = 0; i < LEGS; ++i) {
            const LegTarget &la = blendFrom_.legs[i];
            const LegTarget &lb = blendTo_.legs[i];
            LegTarget &out = lastPose_.legs[i];
            out.joints = la.joints;
            for (int k = 0; k < 3; ++k) out.v[k] = la.v[k] + (lb.v[k] - la.v[k]) * e;
            if (la.joints) continue;
            const float travel = hypotf(lb.v[0] - la.v[0], lb.v[1] - la.v[1]);
            if (travel > STEP_ARC_MIN_TRAVEL_MM)
                out.v[2] += arc * fminf(1.0f, travel / STEP_ARC_FULL_TRAVEL_MM) * sinf((float)M_PI * u);
        }
        if (u >= 1.0f) {
            if (state_ == State::ENTRY) {
                state_ = State::PLAYING;
                t_ = 0.0f;
            } else {
                state_ = State::IDLE;
            }
        }
    }

    void advancePlaying(float dt) {
        const float duration = clip_->duration();
        t_ += dt * params_[SPEED];
        if (clip_->loop) {
            t_ = duration > 0.0f ? fmodf(t_, duration) : 0.0f;
            evaluate(*clip_, params_, t_, kin_, stance(), playBase_, lastPose_);
            return;
        }
        if (t_ < duration) {
            evaluate(*clip_, params_, t_, kin_, stance(), playBase_, lastPose_);
            return;
        }
        ++playsDone_;
        const int repeat = (int)fmaxf(1.0f, floorf(params_[REPEAT] + 0.5f));
        if (playsDone_ < repeat) {
            t_ = duration > 0.0f ? t_ - duration : 0.0f;
            evaluate(*clip_, params_, t_, kin_, stance(), playBase_, lastPose_);
            return;
        }
        evaluate(*clip_, params_, duration, kin_, stance(), playBase_, lastPose_);
        if (clip_->holdEnd) state_ = State::HOLD;
        else
            stop();
    }
};

}  // namespace anim
