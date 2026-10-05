// Host test of animation/animation.h, built and run by test_host_programs.py.
#include <cmath>
#include <cstdio>
#include <animation/animation.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

using namespace anim;

static const KinConfig &pico = KIN_CONFIG_SPOTMICRO_ESP32_MINI;
static Kinematics kin(pico);
static const float base = pico.default_body_height;

// The 12 IK angles of a pose with every leg a foot offset.
struct Angles {
    float a[JOINTS];
    float knee(int leg) const { return a[leg * 3 + 2]; }
    float hip(int leg) const { return a[leg * 3]; }
};

static Angles anglesOf(const float body[6], const float feet[LEGS][3] = nullptr) {
    Pose pose;
    for (int i = 0; i < 6; ++i) pose.body[i] = body[i];
    if (feet)
        for (int leg = 0; leg < LEGS; ++leg)
            for (int k = 0; k < 3; ++k) pose.legs[leg].v[k] = feet[leg][k];
    Angles out;
    poseToAngles(pose, kin, pico.default_feet_positions, base, out.a);
    return out;
}

static const float ZERO[6] = {0, 0, 0, 0, 0, 0};
static const Angles stance = anglesOf(ZERO);
static const int FL = 0, FR = 1, RL = 2, RR = 3;

static float sign(float v) { return v > 0 ? 1.0f : v < 0 ? -1.0f : 0.0f; }

// The knee's angle grows as a foot comes closer to its hip, which pins every axis below to geometry rather than to a
// derivation of Leika's left-handed frame.
static void aLiftedFootBendsItsKnee() {
    float feet[LEGS][3] = {};
    feet[FL][2] = 10;
    const Angles lifted = anglesOf(ZERO, feet);
    CHECK(lifted.knee(FL) > stance.knee(FL));
    CHECK(std::fabs(lifted.knee(FR) - stance.knee(FR)) < 1e-4f);
}

static void raisingTheBodyStraightensEveryKnee() {
    const float body[6] = {0, 0, 0, 0, 0, 10};
    const Angles raised = anglesOf(body);
    for (int leg = 0; leg < LEGS; ++leg) CHECK(raised.knee(leg) < stance.knee(leg));
}

// REP-103: a positive roll lifts the left side, so the left feet hang further below their hips.
static void positiveRollLiftsTheLeftSide() {
    const float body[6] = {0.1f, 0, 0, 0, 0, 0};
    const Angles rolled = anglesOf(body);
    CHECK(rolled.knee(FL) < stance.knee(FL) && rolled.knee(RL) < stance.knee(RL));
    CHECK(rolled.knee(FR) > stance.knee(FR) && rolled.knee(RR) > stance.knee(RR));
}

// REP-103: a positive pitch lowers the nose, so the front feet come closer to their hips.
static void positivePitchLowersTheNose() {
    const float body[6] = {0, 0.1f, 0, 0, 0, 0};
    const Angles pitched = anglesOf(body);
    CHECK(pitched.knee(FL) > stance.knee(FL) && pitched.knee(FR) > stance.knee(FR));
    CHECK(pitched.knee(RL) < stance.knee(RL) && pitched.knee(RR) < stance.knee(RR));
}

// REP-103: a positive yaw turns the body left over its planted feet, so under the body the front feet move right and
// the rear feet left. Each hip must turn the way it does for its foot moved that way.
static void positiveYawTurnsLeft() {
    const float body[6] = {0, 0, 0.1f, 0, 0, 0};
    const Angles yawed = anglesOf(body);
    for (int leg = 0; leg < LEGS; ++leg) {
        const bool front = leg == FL || leg == FR;
        float feet[LEGS][3] = {};
        feet[leg][1] = front ? -5.0f : 5.0f;
        const Angles moved = anglesOf(ZERO, feet);
        CHECK(sign(yawed.hip(leg) - stance.hip(leg)) == sign(moved.hip(leg) - stance.hip(leg)));
        CHECK(std::fabs(yawed.hip(leg) - stance.hip(leg)) > 0.1f);
    }
}

// Moving the body forward or left is moving every foot back or right by as much.
static void aBodyShiftIsTheOppositeShiftOfEveryFoot() {
    for (int axis = 0; axis < 2; ++axis) {
        float body[6] = {0, 0, 0, 0, 0, 0};
        body[X + axis] = 10;
        float feet[LEGS][3] = {};
        for (int leg = 0; leg < LEGS; ++leg) feet[leg][axis] = -10;
        const Angles shifted = anglesOf(body);
        const Angles stepped = anglesOf(ZERO, feet);
        for (int j = 0; j < JOINTS; ++j) CHECK(std::fabs(shifted.a[j] - stepped.a[j]) < 1e-3f);
    }
}

static void aCapturedPoseMapsBackToItsBodyState() {
    body_state_t b;
    const float body[6] = {0.05f, -0.03f, 0.02f, 4, -6, 8};
    toBodyState(body, pico.default_feet_positions, base, b);
    const float foot[3] = {3, -2, 5};
    addFootOffset(b, RL, foot);
    Pose pose;
    capturePose(b, pico.default_feet_positions, base, pose);
    for (int a = 0; a < 6; ++a) CHECK(std::fabs(pose.body[a] - body[a]) < 1e-4f);
    for (int k = 0; k < 3; ++k) CHECK(std::fabs(pose.legs[RL].v[k] - foot[k]) < 1e-3f);
    CHECK(std::fabs(pose.legs[FL].v[0]) < 1e-4f);
}

static void anUnreachableFootIsReported() {
    float feet[LEGS][3] = {};
    feet[RR][2] = -500;
    Pose pose;
    for (int k = 0; k < 3; ++k) pose.legs[RR].v[k] = feet[RR][k];
    float angles[JOINTS];
    CHECK(poseToAngles(pose, kin, pico.default_feet_positions, base, angles) == 0x6u << (RR * 3));
    Pose rest;
    CHECK(poseToAngles(rest, kin, pico.default_feet_positions, base, angles) == 0);
}

static Clip twoKeyframes(float bodyZ) {
    Clip c;
    std::strcpy(c.name, "bob");
    c.keyframeCount = 2;
    c.keyframes[0].time = 0;
    c.keyframes[1].time = 1;
    c.keyframes[1].body[Z] = bodyZ;
    return c;
}

static void theValidatorKnowsFourLegs() {
    Clip c = twoKeyframes(10);
    CHECK(validate(c) == nullptr);
    c.keyframes[1].legCount = 6;
    CHECK(validate(c) != nullptr);
    c.keyframes[1].legCount = 4;
    CHECK(validate(c) == nullptr);
    c.overlayCount = 1;
    c.overlays[0] = {CHANNEL_FOOT, 12, 1, 1, 0, 0, 1};
    CHECK(validate(c) != nullptr);
    c.overlays[0].channel = 11;
    CHECK(validate(c) == nullptr);
    c.hasRideHeight = true;
    c.rideHeight = 0;
    CHECK(validate(c) != nullptr);
}

static void evaluationInterpolatesAndAppliesOverlaysAndLift() {
    Clip c = twoKeyframes(10);
    c.keyframes[1].legCount = LEGS;
    c.keyframes[1].legs[FL].v[2] = 20;
    c.overlayCount = 1;
    c.overlays[0] = {CHANNEL_BODY, X, 4, 0.25f, 0, 0, 1};
    float params[PARAM_COUNT];
    const ParamValue lift[] = {{FOOT_LIFT, 0.5f}};
    c.paramCount = 1;
    c.params[0] = {FOOT_LIFT, 0, 1, 2};
    resolveParams(c, lift, 1, params);
    Pose out;
    evaluate(c, params, 0.5f, kin, pico.default_feet_positions, base, out);
    CHECK(std::fabs(out.body[Z] - 5) < 1e-4f);
    CHECK(std::fabs(out.body[X] - 4 * std::sin(2 * (float)M_PI * 0.25f * 0.5f)) < 1e-4f);
    CHECK(std::fabs(out.legs[FL].v[2] - 5) < 1e-4f);
}

static int ticksUntil(Player &p, State state, float dt, int limit = 1000) {
    for (int i = 0; i < limit; ++i) {
        if (p.state() == state) return i;
        p.update(dt);
    }
    return -1;
}

// From the live pose through the clip and back to stance at the height the body started from.
static void aClipEntersPlaysAndExitsToStance() {
    Clip c = twoKeyframes(10);
    Player p(pico, kin);
    Pose live;
    live.body[Z] = -4;
    p.play(&c, nullptr, 0, live, base);
    CHECK(p.state() == State::ENTRY);
    CHECK(std::fabs(p.update(0.01f).body[Z] - (-4)) < 0.1f);
    const int entry = ticksUntil(p, State::PLAYING, 0.01f);
    CHECK(entry >= 48 && entry <= 50);
    const int playing = ticksUntil(p, State::EXIT, 0.01f);
    CHECK(playing >= 99 && playing <= 101);
    CHECK(ticksUntil(p, State::IDLE, 0.01f) > 0);
    const Pose &end = p.lastPose();
    for (float v : end.body) CHECK(std::fabs(v) < 1e-4f);
    CHECK(std::fabs(p.base() - base) < 1e-6f);
}

static void aClipWithItsOwnHeightMovesTheBaseThereAndBack() {
    Clip c = twoKeyframes(0);
    c.hasRideHeight = true;
    c.rideHeight = 60;
    Player p(pico, kin);
    p.play(&c, nullptr, 0, Pose {}, base);
    ticksUntil(p, State::PLAYING, 0.01f);
    CHECK(std::fabs(p.base() - 0.060f) < 1e-6f);
    ticksUntil(p, State::IDLE, 0.01f);
    CHECK(std::fabs(p.base() - base) < 1e-6f);
}

static void repeatPlaysTheClipAgainAndHoldFreezesIt() {
    Clip c = twoKeyframes(10);
    c.paramCount = 1;
    c.params[0] = {REPEAT, 1, 1, 5};
    const ParamValue twice[] = {{REPEAT, 2}};
    Player p(pico, kin);
    p.play(&c, twice, 1, Pose {}, base);
    ticksUntil(p, State::PLAYING, 0.01f);
    const int playing = ticksUntil(p, State::EXIT, 0.01f);
    CHECK(playing >= 199 && playing <= 201);

    Clip held = twoKeyframes(10);
    held.holdEnd = true;
    Player h(pico, kin);
    h.play(&held, nullptr, 0, Pose {}, base);
    CHECK(ticksUntil(h, State::HOLD, 0.01f) > 0);
    for (int i = 0; i < 100; ++i) h.update(0.01f);
    CHECK(h.state() == State::HOLD && std::fabs(h.lastPose().body[Z] - 10) < 1e-4f);
    h.stop();
    CHECK(h.state() == State::EXIT);
}

int main() {
    aLiftedFootBendsItsKnee();
    raisingTheBodyStraightensEveryKnee();
    positiveRollLiftsTheLeftSide();
    positivePitchLowersTheNose();
    positiveYawTurnsLeft();
    aBodyShiftIsTheOppositeShiftOfEveryFoot();
    aCapturedPoseMapsBackToItsBodyState();
    anUnreachableFootIsReported();
    theValidatorKnowsFourLegs();
    evaluationInterpolatesAndAppliesOverlaysAndLift();
    aClipEntersPlaysAndExitsToStance();
    aClipWithItsOwnHeightMovesTheBaseThereAndBack();
    repeatPlaysTheClipAgainAndHoldFreezesIt();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
