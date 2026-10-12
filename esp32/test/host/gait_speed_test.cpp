// Host test of the walk's reach and joint speeds, built and run by test_host_programs.py: no gait, at any full stick,
// speed, step height or body height, puts a foot where its leg cannot reach or commands a joint faster than
// WalkState::MAX_JOINT_SPEED_DEG_S, or its cap at a lower servo supply.
// With the argument "report" it prints each gait's peak commanded joint speed, how often the servos' speed limit
// (SERVO_MAX_SPEED_DEG_S) holds a joint back, its forward speed at the app's settings and the commands out of reach.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <variant.h>
#include <motion_states/walk_state.h>
#include <peripherals/servo_output.h>

static int failures = 0;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);     \
            failures++;                                                          \
        }                                                                        \
    } while (0)

namespace {

// The motion task's tick (esp32/src/main.cpp).
constexpr float DT = 0.01f;
// A knee the walk holds at its bound measures a little past it: the bound checks the swing at only some phases.
constexpr float BEND_SAMPLING_DEG = 0.1f;

enum Gait { TROT, CRAWL };

struct Speeds {
    float peak_deg_s = 0;
    int clipped_ticks = 0;
    int ticks = 0;
    // When the front left foot lifted off, first and last, and how often: the cycles walked.
    int lifts = 0, first_lift = 0, last_lift = 0;

    // How far the body walked forward: each tick, by as much as its stance feet moved back.
    float forward_m = 0;

    float cyclesPerSecond(float dt) const { return lifts > 1 ? (lifts - 1) / ((last_lift - first_lift) * dt) : 0; }
    float forwardSpeed(float dt) const { return forward_m / (ticks * dt); }
    // Every foot stayed where its leg can put it, with the knee between the bends the walk keeps it in. A straight knee
    // is where the kinematics clamp a foot beyond reach; a knee folded further, or a hip turned past 90, has the foot at
    // the hip, where the hip turns without bound and a joint jumps however slowly the walk goes.
    bool reached = true;
};

// Walks from the stand, as MotionService does: the state steps the body and the kinematics turn it into joint angles.
// The commanded angles go through the servos' speed limit the way ServoController does, and a tick counts as clipped
// when the limit holds any joint back.
struct Walk {
    const KinConfig &config;
    WalkState walk;
    Kinematics kinematics;
    body_state_t body;
    float commanded[12] = {0};
    float output[12] = {0};
    float lifted = 0;
    Speeds speeds;

    Walk(const KinConfig &config, Gait gait) : config(config), kinematics(config) {
        walk.configure(config);
        if (gait == CRAWL) walk.set_mode_crawl();
        body.ym = config.default_body_height;
        body.updateFeet(config.default_feet_positions);
        kinematics.calculate_inverse_kinematics(body, commanded);
        std::memcpy(output, commanded, sizeof(output));
        walk.resetSmoothing();
        walk.begin();
    }

    // Out to the stand and back, as MotionService switches states: the stand has put every foot back on its spot.
    void reenter() {
        walk.end();
        body.updateFeet(config.default_feet_positions);
        kinematics.calculate_inverse_kinematics(body, commanded);
        std::memcpy(output, commanded, sizeof(output));
        walk.resetSmoothing();
        walk.begin();
    }

    void hold(const CommandMsg &cmd, float seconds) {
        static_cast<MotionState &>(walk).handleCommand(cmd);
        const float max_step = SERVO_MAX_SPEED_DEG_S * DT;
        for (int tick = 0; tick < std::lround(seconds / DT); tick++) {
            const body_state_t before = body;
            walk.step(body, DT);
            float moved = 0;
            int stance = 0;
            for (int leg = 0; leg < 4; leg++)
                if (std::max(before.feet[leg][1], body.feet[leg][1]) <= 1e-6f) {
                    moved += before.feet[leg][0] - body.feet[leg][0];
                    stance++;
                }
            if (stance) speeds.forward_m += moved / stance;
            float angles[12];
            kinematics.calculate_inverse_kinematics(body, angles);
            bool clipped = false;
            for (int joint = 0; joint < 12; joint++) {
                speeds.peak_deg_s = std::max(speeds.peak_deg_s, std::fabs(angles[joint] - commanded[joint]) / DT);
                clipped |= std::fabs(angles[joint] - output[joint]) > max_step + 1e-4f;
                output[joint] = slewToward(output[joint], angles[joint], max_step);
            }
            for (int leg = 0; leg < 4; leg++) {
                const float *joint = angles + 3 * leg;
                const float knee = config.kneeFollowsFemur ? joint[2] - joint[1] : joint[2];
                speeds.reached &= knee >= WalkState::MIN_KNEE_BEND_DEG - BEND_SAMPLING_DEG &&
                                  knee <= WalkState::MAX_KNEE_BEND_DEG + BEND_SAMPLING_DEG && std::fabs(joint[0]) < 90;
            }
            if (lifted <= 0 && body.feet[0][1] > 0) {
                if (!speeds.lifts++) speeds.first_lift = speeds.ticks;
                speeds.last_lift = speeds.ticks;
            }
            lifted = body.feet[0][1];
            std::memcpy(commanded, angles, sizeof(commanded));
            speeds.clipped_ticks += clipped;
            speeds.ticks++;
        }
    }
};

// lx, ly, rx, ry, h, s, s1
CommandMsg command(float lx, float ly, float rx, float h, float s, float s1) { return {lx, ly, rx, 0, h, s, s1}; }

// Two cycles at the slowest cadence, or as many as asked: a crawl cycle then takes 4 s, a trot cycle 1 s.
Speeds walkThere(const KinConfig &config, Gait gait, const CommandMsg &cmd, int cycles = 2) {
    Walk walk(config, gait);
    walk.hold(cmd, cycles * (gait == CRAWL ? 4.f : 1.f));
    return walk.speeds;
}

// The same, measured only once the command smoothing has settled.
Speeds settledWalk(const KinConfig &config, Gait gait, const CommandMsg &cmd, int cycles) {
    Walk walk(config, gait);
    walk.hold(cmd, 1);
    walk.speeds = Speeds();
    walk.hold(cmd, cycles * (gait == CRAWL ? 4.f : 1.f));
    return walk.speeds;
}

// Full sticks: the left stick to the rim in eight directions and to the corners a gamepad reaches, the right stick
// turning either way, and both at once.
constexpr float SQRT_HALF = 0.70710678f;
constexpr float STICKS[][3] = {{0, 1, 0},  {0, -1, 0}, {1, 0, 0},  {-1, 0, 0}, {SQRT_HALF, SQRT_HALF, 0},
                               {-SQRT_HALF, -SQRT_HALF, 0}, {1, 1, 0}, {-1, 1, 0}, {0, 0, 1},  {0, 0, -1},
                               {0, 1, 1},  {1, 0, -1}, {1, 1, 1}};
constexpr float LEVELS[] = {0, 0.25f, 0.5f, 0.75f, 1};
constexpr float SPEEDS[] = {0, 0.5f, 1};
constexpr KinematicsVariant VARIANTS[] = {socket_message_KinematicsVariant_SPOTMICRO_ESP32,
                                          socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI,
                                          socket_message_KinematicsVariant_SPOTMICRO_YERTLE};
// The app's and the Python client's height and step height.
constexpr float DEFAULT_H = 0.7f, DEFAULT_S1 = 0.5f;
// A joint the walk holds at the cap measures a hair above it: the speed is a difference of floats divided by the tick.
constexpr float ROUNDING_DEG_S = 0.01f;

const char *gaitName(Gait gait) { return gait == TROT ? "trot" : "crawl"; }

// Every full stick at every speed, step height and body height keeps every foot in reach and stays under the cap.
void everyFullStickIsReachableAndUnderTheCap() {
    for (const KinematicsVariant variant : VARIANTS) {
        const KinConfig &config = *kinConfigFor(variant);
        for (const Gait gait : {TROT, CRAWL}) {
            for (const auto &stick : STICKS)
                for (const float h : LEVELS)
                    for (const float s1 : LEVELS)
                        for (const float s : SPEEDS) {
                            const Speeds speeds = walkThere(config, gait, command(stick[0], stick[1], stick[2], h, s, s1));
                            const bool fast = speeds.peak_deg_s > WalkState::MAX_JOINT_SPEED_DEG_S + ROUNDING_DEG_S;
                            if (!speeds.reached || fast)
                                std::printf("%s %s stick %g,%g,%g h %g s %g s1 %g: %s, %.0f deg/s\n",
                                            variantName(variant), gaitName(gait), stick[0], stick[1], stick[2], h, s,
                                            s1, speeds.reached ? "in reach" : "out of reach", speeds.peak_deg_s);
                            CHECK(speeds.reached);
                            CHECK(!fast);
                        }
        }
    }
}

// A short step is walked at the cadence it was asked for: the cap only slows the steps that need it.
void aShortStepKeepsItsCadence() {
    for (const KinematicsVariant variant : VARIANTS) {
        const CommandMsg cmd = command(0, 0.1f, 0, DEFAULT_H, 0.5f, 0.1f);
        Walk walk(*kinConfigFor(variant), TROT);
        walk.hold(cmd, 2);
        // A trot at half speed takes 1 s a cycle, so 1 s on every foot is where it was.
        const body_state_t before = walk.body;
        walk.hold(cmd, 1);
        CHECK(walk.speeds.peak_deg_s < WalkState::MAX_JOINT_SPEED_DEG_S);
        for (int leg = 0; leg < 4; leg++)
            for (int axis = 0; axis < 3; axis++)
                CHECK(std::fabs(walk.body.feet[leg][axis] - before.feet[leg][axis]) < 1e-4f);
    }
}

// The reach bound shortens only the strokes the legs cannot reach: a full forward trot at the app's settings keeps its
// whole stroke on every variant, so the body walks the step length in each stance.
void aFullForwardTrotKeepsItsStroke() {
    constexpr float TROT_DUTY = 0.75f;
    for (const KinematicsVariant variant : VARIANTS) {
        const KinConfig &config = *kinConfigFor(variant);
        const Speeds forward = settledWalk(config, TROT, command(0, 1, 0, DEFAULT_H, 0.5f, DEFAULT_S1), 8);
        const float stroked = forward.forwardSpeed(DT) * TROT_DUTY / forward.cyclesPerSecond(DT);
        if (std::fabs(stroked / config.max_step_length - 1) > 0.02f)
            std::printf("%s: strokes %.4f m of %.4f\n", variantName(variant), stroked, config.max_step_length);
        CHECK(std::fabs(stroked / config.max_step_length - 1) <= 0.02f);
    }
}

// Letting go of the stick mid-stride lands every raised foot along its swing and then holds still: resetting the phase
// dropped a foot from its full height in one tick, 3,585 deg/s on the Pico.
void stoppingLandsEveryFootWithoutAJump() {
    for (const KinematicsVariant variant : VARIANTS) {
        const KinConfig &config = *kinConfigFor(variant);
        for (const Gait gait : {TROT, CRAWL}) {
            const float cycle = gait == CRAWL ? 4.f : 1.f;
            for (int moment = 0; moment < 16; moment++) {
                Walk walk(config, gait);
                walk.hold(command(0, 1, 0, DEFAULT_H, 0.5f, DEFAULT_S1), cycle * (1 + moment / 16.f));
                walk.speeds = Speeds();
                walk.hold(command(0, 0, 0, DEFAULT_H, 0.5f, DEFAULT_S1), 2 * cycle);
                const body_state_t settled = walk.body;
                walk.hold(command(0, 0, 0, DEFAULT_H, 0.5f, DEFAULT_S1), 0.5f);
                if (walk.speeds.peak_deg_s > WalkState::MAX_JOINT_SPEED_DEG_S + ROUNDING_DEG_S)
                    std::printf("%s %s stopped at %d/16: %.0f deg/s\n", variantName(variant), gaitName(gait), moment,
                                walk.speeds.peak_deg_s);
                CHECK(walk.speeds.peak_deg_s <= WalkState::MAX_JOINT_SPEED_DEG_S + ROUNDING_DEG_S);
                for (int leg = 0; leg < 4; leg++) {
                    CHECK(std::fabs(walk.body.feet[leg][1] - config.default_feet_positions[leg][1]) < 0.003f);
                    for (int axis = 0; axis < 3; axis++)
                        CHECK(std::fabs(walk.body.feet[leg][axis] - settled.feet[leg][axis]) < 1e-5f);
                }
            }
        }
    }
}

// A walk entered from the stand, or switched to the other gait while it stands, starts with every foot down: the phase
// it held, left mid-stride or read with the other gait's offsets, had a foot in the air, which went up in one tick at
// up to 3,700 deg/s. The same gait sent again mid-stride keeps its step.
void enteringOrSwitchingAGaitLiftsNoFootAtOnce() {
    const CommandMsg forward = command(0, 1, 0, DEFAULT_H, 0.5f, DEFAULT_S1);
    const CommandMsg still = command(0, 0, 0, DEFAULT_H, 0.5f, DEFAULT_S1);
    for (const KinematicsVariant variant : VARIANTS) {
        const KinConfig &config = *kinConfigFor(variant);
        const auto checkUnderCap = [&](const Walk &walk, Gait gait, const char *how, int moment) {
            const bool under = walk.speeds.peak_deg_s <= WalkState::MAX_JOINT_SPEED_DEG_S + ROUNDING_DEG_S;
            if (!under)
                std::printf("%s %s %s at %d/16: %.0f deg/s\n", variantName(variant), gaitName(gait), how, moment,
                            walk.speeds.peak_deg_s);
            CHECK(under);
        };
        for (const Gait gait : {TROT, CRAWL}) {
            const float cycle = gait == CRAWL ? 4.f : 1.f;
            for (int moment = 0; moment < 16; moment++) {
                const float walked = cycle * (1 + moment / 16.f);

                Walk reentered(config, gait);
                reentered.hold(forward, walked);
                reentered.reenter();
                reentered.speeds = Speeds();
                reentered.hold(still, 0.5f);
                reentered.hold(forward, cycle);
                checkUnderCap(reentered, gait, "left mid-stride and entered again", moment);

                Walk switched(config, gait);
                switched.hold(forward, walked);
                switched.hold(still, 2 * cycle);
                switched.speeds = Speeds();
                if (gait == TROT)
                    switched.walk.set_mode_crawl();
                else
                    switched.walk.set_mode_trot();
                switched.hold(still, 0.5f);
                switched.hold(forward, cycle);
                checkUnderCap(switched, gait, "stopped and switched", moment);

                Walk resent(config, gait);
                resent.hold(forward, walked);
                resent.speeds = Speeds();
                if (gait == TROT)
                    resent.walk.set_mode_trot();
                else
                    resent.walk.set_mode_crawl();
                resent.hold(forward, cycle);
                checkUnderCap(resent, gait, "sent its gait again mid-stride", moment);
            }
        }
    }
}

// A lower supply, told to the walk mid-stride, caps the joints at its own share from the next tick on, and the walk
// keeps its stroke and steps slower; every full stick stays under the lower cap.
void aLowerSupplyCapsTheWalkFromTheNextTick() {
    constexpr float LOW_VOLTS = 4.8f;
    const float lowCap = WalkState::JOINT_SPEED_SHARE * servoSpeedLimit(LOW_VOLTS);
    const CommandMsg forward = command(0, 1, 0, DEFAULT_H, 0.5f, DEFAULT_S1);
    const KinConfig &mini = *kinConfigFor(socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI);
    Walk nominal(mini, TROT), lowered(mini, TROT);
    nominal.hold(forward, 2);
    lowered.hold(forward, 2);
    CHECK(lowered.speeds.peak_deg_s > lowCap + ROUNDING_DEG_S);  // the full supply's walk would outrun the low cap
    lowered.walk.useServoSpeedLimit(servoSpeedLimit(LOW_VOLTS));
    CHECK(lowered.walk.maxJointSpeed() == lowCap);
    nominal.speeds = lowered.speeds = Speeds();
    nominal.hold(forward, 8);
    lowered.hold(forward, 8);
    CHECK(lowered.speeds.peak_deg_s <= lowCap + ROUNDING_DEG_S);
    CHECK(lowered.speeds.cyclesPerSecond(DT) < 0.9f * nominal.speeds.cyclesPerSecond(DT));
    const float nominalStroke = nominal.speeds.forwardSpeed(DT) / nominal.speeds.cyclesPerSecond(DT);
    const float loweredStroke = lowered.speeds.forwardSpeed(DT) / lowered.speeds.cyclesPerSecond(DT);
    CHECK(std::fabs(loweredStroke / nominalStroke - 1) < 0.02f);

    for (const KinematicsVariant variant : VARIANTS)
        for (const Gait gait : {TROT, CRAWL})
            for (const auto &stick : STICKS) {
                Walk walk(*kinConfigFor(variant), gait);
                walk.walk.useServoSpeedLimit(servoSpeedLimit(LOW_VOLTS));
                walk.hold(command(stick[0], stick[1], stick[2], DEFAULT_H, 1, 1), gait == CRAWL ? 8.f : 2.f);
                CHECK(walk.speeds.peak_deg_s <= lowCap + ROUNDING_DEG_S);
            }
}

// A walk never told a supply caps at the default's share, as before the supply was a setting.
void theDefaultSupplyKeepsTheCap() {
    WalkState walk;
    CHECK(walk.maxJointSpeed() == WalkState::MAX_JOINT_SPEED_DEG_S);
    CHECK(WalkState::MAX_JOINT_SPEED_DEG_S == 540);
    walk.useServoSpeedLimit(servoSpeedLimit(DEFAULT_SERVO_SUPPLY_VOLTS));
    CHECK(walk.maxJointSpeed() == WalkState::MAX_JOINT_SPEED_DEG_S);
}

void report() {
    std::printf("variant gait s: forward full stick at h %g, s1 %g: peak deg/s, ticks clipped at %.0f deg/s, cycles/s, m/s | "
                "every reachable full stick, h and s1: worst peak, worst clipped share, unreachable commands\n",
                DEFAULT_H, DEFAULT_S1, SERVO_MAX_SPEED_DEG_S);
    for (const KinematicsVariant variant : VARIANTS) {
        const KinConfig &config = *kinConfigFor(variant);
        for (const Gait gait : {TROT, CRAWL}) {
            for (const float s : SPEEDS) {
                const Speeds forward = settledWalk(config, gait, command(0, 1, 0, DEFAULT_H, s, DEFAULT_S1), 8);
                float worst = 0, worst_clipped = 0;
                int unreachable = 0, commands = 0;
                for (const auto &stick : STICKS)
                    for (const float h : LEVELS)
                        for (const float s1 : LEVELS) {
                            const Speeds speeds = walkThere(config, gait, command(stick[0], stick[1], stick[2], h, s, s1));
                            commands++;
                            if (!speeds.reached) {
                                unreachable++;
                                continue;
                            }
                            worst = std::max(worst, speeds.peak_deg_s);
                            worst_clipped = std::max(worst_clipped, float(speeds.clipped_ticks) / speeds.ticks);
                        }
                std::printf("%s %s %g: %.0f, %.1f%%, %.2f, %.3f | %.0f, %.1f%%, %d of %d\n", variantName(variant),
                            gaitName(gait), s, forward.peak_deg_s, 100.f * forward.clipped_ticks / forward.ticks,
                            forward.cyclesPerSecond(DT), forward.forwardSpeed(DT), worst, 100.f * worst_clipped,
                            unreachable, commands);
            }
        }
    }
}

}  // namespace

int main(int argc, char **argv) {
    if (argc > 1 && std::strcmp(argv[1], "report") == 0) {
        report();
        return 0;
    }
    everyFullStickIsReachableAndUnderTheCap();
    aShortStepKeepsItsCadence();
    aFullForwardTrotKeepsItsStroke();
    stoppingLandsEveryFootWithoutAJump();
    enteringOrSwitchingAGaitLiftsNoFootAtOnce();
    theDefaultSupplyKeepsTheCap();
    aLowerSupplyCapsTheWalkFromTheNextTick();
    if (failures) std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
