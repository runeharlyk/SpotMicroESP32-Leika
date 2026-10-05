// Plays clips through the firmware's anim::Player and prints every tick as JSON, so the web app's TypeScript port
// (app/src/lib/animation/player.ts) can be pinned to it. Run once per kinematics variant by
// export_animation_traces.py, which passes the variant's name.
#include <cstdio>
#include <cstring>
#include <vector>
#include <pb_decode.h>
#include <animation/animation_codec.h>
#include <animation/builtin_clips.h>
#include <variant.h>

namespace {

constexpr float DT = 0.01f;
constexpr int MAX_TICKS = 700;
constexpr int TICKS_AFTER_IDLE = 5;

struct Case {
    const char *label;
    anim::Clip clip;
    std::vector<anim::ParamValue> params;
    int stopAt = -1;  // the tick before whose update stop() is called, or none
};

const KinConfig *configNamed(const char *name) {
    for (int variant = 0; variant <= socket_message_KinematicsVariant_SPOTMICRO_YERTLE; variant++) {
        const auto known = static_cast<KinematicsVariant>(variant);
        if (knownVariant(known) && std::strcmp(variantName(known), name) == 0) return kinConfigFor(known);
    }
    return nullptr;
}

anim::Clip builtin(const char *name) {
    static animation_Animation message;
    anim::Clip clip;
    for (const anim::BuiltinClip &b : anim::BUILTIN_CLIPS) {
        if (std::strcmp(b.name, name) != 0) continue;
        message = animation_Animation_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(b.data, b.size);
        pb_decode(&stream, animation_Animation_fields, &message);
        anim::fromProto(message, clip);
    }
    return clip;
}

anim::Clip named(const char *name) {
    anim::Clip c;
    std::strcpy(c.name, name);
    return c;
}

anim::Keyframe at(float time, int ease = anim::LINEAR) {
    anim::Keyframe k;
    k.time = time;
    k.ease = ease;
    return k;
}

void footLegs(anim::Keyframe &k) {
    k.legCount = anim::LEGS;
    for (anim::LegTarget &leg : k.legs) leg = {};
}

// Leg 1 goes from a foot target to joint angles and back, so the evaluator's mixed path and the blends' joint
// conversion both run. The joint angles are the variant's own for a raised foot, nudged, so every variant reaches them.
anim::Clip mixed(Kinematics &kin, const KinConfig &config) {
    anim::Clip c = named("mixed");
    c.keyframeCount = 4;
    c.keyframes[0] = at(0);
    c.keyframes[1] = at(0.6f, anim::EASE_IN_OUT);
    footLegs(c.keyframes[1]);
    c.keyframes[1].legs[1].v[0] = 10;
    c.keyframes[1].legs[1].v[2] = 20;
    c.keyframes[2] = at(1.2f, anim::EASE_OUT);
    c.keyframes[2].body[anim::PITCH] = 0.05f;
    footLegs(c.keyframes[2]);
    const float raised[3] = {12, 0, 25};
    float joints[3];
    anim::legJointsDeg(kin, c.keyframes[2].body, raised, 1, config.default_feet_positions, config.default_body_height,
                       joints);
    c.keyframes[2].legs[1] = {true, {joints[0] + 3, joints[1] - 4, joints[2] + 5}};
    c.keyframes[3] = at(1.8f, anim::EASE_IN);
    footLegs(c.keyframes[3]);
    c.overlayCount = 1;
    c.overlays[0] = {anim::CHANNEL_FOOT, 1 * 3 + 2, 6, 2, 0.3f, 0.2f, 0.6f};
    return c;
}

anim::Clip looping() {
    anim::Clip c = named("loop");
    c.loop = true;
    c.keyframeCount = 2;
    c.keyframes[0] = at(0);
    c.keyframes[1] = at(1, anim::EASE_IN_OUT);
    c.keyframes[1].body[anim::YAW] = 0.1f;
    c.keyframes[1].body[anim::Y] = -8;
    c.overlayCount = 1;
    c.overlays[0] = {anim::CHANNEL_BODY, anim::ROLL, 0.04f, 1.5f, 0, 0, 1};
    return c;
}

anim::Clip repeating(const KinConfig &config) {
    anim::Clip c = named("repeat");
    c.hasRideHeight = true;
    c.rideHeight = 0.9f * config.default_body_height / anim::MM;
    c.entryTime = 0.3f;
    c.exitTime = 0.7f;
    c.keyframeCount = 3;
    c.keyframes[0] = at(0);
    c.keyframes[1] = at(0.4f, anim::EASE_OUT);
    c.keyframes[1].body[anim::Z] = -10;
    c.keyframes[1].body[anim::X] = 8;
    c.keyframes[2] = at(0.8f, anim::EASE_IN);
    c.paramCount = 3;
    c.params[0] = {anim::REPEAT, 1, 2, 3};
    c.params[1] = {anim::SPEED, 0.5f, 1, 2};
    c.params[2] = {anim::BODY_Z, 0, 1, 1.5f};
    return c;
}

// Raises the body past the legs' reach for a while, so the clamp mask is set on every leg and cleared again.
anim::Clip stretching(const KinConfig &config) {
    anim::Clip c = named("stretch");
    c.keyframeCount = 3;
    c.keyframes[0] = at(0);
    c.keyframes[1] = at(0.5f, anim::EASE_IN_OUT);
    c.keyframes[1].body[anim::Z] = (config.max_leg_reach - config.default_body_height) / anim::MM + 15;
    c.keyframes[2] = at(1);
    return c;
}

void printFloats(const float *values, int count) {
    for (int i = 0; i < count; i++) printf("%s%.9g", i ? "," : "", values[i]);
}

void printPose(const anim::Pose &p) {
    printf("{\"body\":[");
    printFloats(p.body, 6);
    printf("],\"legs\":[");
    for (int i = 0; i < anim::LEGS; i++) {
        printf("%s{\"joints\":%s,\"v\":[", i ? "," : "", p.legs[i].joints ? "true" : "false");
        printFloats(p.legs[i].v, 3);
        printf("]}");
    }
    printf("]}");
}

// The clip in animation.proto's JSON names, as the app's Animation.fromJSON reads it.
void printClip(const anim::Clip &c) {
    static const char *BODY[6] = {"roll", "pitch", "yaw", "x", "y", "z"};
    printf("{\"name\":\"%s\",\"schema\":%u,\"loop\":%s,\"holdEnd\":%s,\"entryTime\":%.9g,\"exitTime\":%.9g", c.name,
           (unsigned)c.schema, c.loop ? "true" : "false", c.holdEnd ? "true" : "false", c.entryTime, c.exitTime);
    if (c.hasRideHeight) printf(",\"rideHeight\":%.9g", c.rideHeight);
    printf(",\"keyframes\":[");
    for (int i = 0; i < c.keyframeCount; i++) {
        const anim::Keyframe &k = c.keyframes[i];
        printf("%s{\"time\":%.9g,\"ease\":%d,\"body\":{", i ? "," : "", k.time, k.ease);
        for (int a = 0; a < 6; a++) printf("%s\"%s\":%.9g", a ? "," : "", BODY[a], k.body[a]);
        printf("},\"legs\":[");
        for (int l = 0; l < k.legCount; l++) {
            const float *v = k.legs[l].v;
            if (k.legs[l].joints)
                printf("%s{\"joints\":{\"coxa\":%.9g,\"femur\":%.9g,\"tibia\":%.9g}}", l ? "," : "", v[0], v[1], v[2]);
            else
                printf("%s{\"foot\":{\"x\":%.9g,\"y\":%.9g,\"z\":%.9g}}", l ? "," : "", v[0], v[1], v[2]);
        }
        printf("]}");
    }
    printf("],\"overlays\":[");
    for (int i = 0; i < c.overlayCount; i++) {
        const anim::Overlay &o = c.overlays[i];
        printf("%s{\"%s\":%d,\"amplitude\":%.9g,\"frequency\":%.9g,\"phase\":%.9g,\"start\":%.9g,\"end\":%.9g}",
               i ? "," : "", o.kind == anim::CHANNEL_BODY ? "bodyAxis" : "footChannel", o.channel, o.amplitude,
               o.frequency, o.phase, o.start, o.end);
    }
    printf("],\"params\":[");
    for (int i = 0; i < c.paramCount; i++) {
        const anim::ParamSpec &p = c.params[i];
        printf("%s{\"id\":%d,\"min\":%.9g,\"defaultValue\":%.9g,\"max\":%.9g}", i ? "," : "", p.id, p.min,
               p.defaultValue, p.max);
    }
    printf("]}");
}

}  // namespace

int main(int argc, char **argv) {
    const KinConfig *config = argc > 1 ? configNamed(argv[1]) : nullptr;
    if (!config) {
        fprintf(stderr, "usage: animation_trace <variant name>\n");
        return 2;
    }
    const float(*stance)[4] = config->default_feet_positions;
    Kinematics kin(*config);

    std::vector<Case> cases = {
        {"sit, stopped while it holds", builtin("sit"), {}, 300},
        {"bow at its defaults", builtin("bow"), {}},
        {"wave, stopped while it waves", builtin("wave"), {{anim::OVERLAY_AMPLITUDE, 1.4f}}, 250},
        {"mixed foot and joint targets", mixed(kin, *config), {}},
        {"loop, stopped", looping(), {}, 260},
        {"repeat at speed 1.5 and a ride height", repeating(*config),
         {{anim::SPEED, 1.5f}, {anim::BODY_Z, 1.3f}, {anim::REPEAT, 2}}},
        {"stretch past reach", stretching(*config), {}},
    };

    // The live pose a clip starts from: standing, with one foot set off so Entry has a step to take.
    body_state_t standing;
    standing.ym = config->default_body_height;
    standing.updateFeet(stance);
    const float shifted[3] = {6, -4, 0};
    anim::addFootOffset(standing, 3, shifted);
    anim::Pose live;
    anim::capturePose(standing, stance, standing.ym, live);

    printf("{\"variant\":\"%s\",\"dt\":%.9g,\"cases\":[", argv[1], DT);
    for (size_t n = 0; n < cases.size(); n++) {
        const Case &c = cases[n];
        if (const char *error = anim::validate(c.clip)) {
            fprintf(stderr, "%s: %s\n", c.label, error);
            return 1;
        }
        printf("%s{\"label\":\"%s\",\"clip\":", n ? "," : "", c.label);
        printClip(c.clip);
        printf(",\"params\":[");
        for (size_t i = 0; i < c.params.size(); i++)
            printf("%s{\"id\":%d,\"value\":%.9g}", i ? "," : "", c.params[i].id, c.params[i].value);
        printf("],\"live\":");
        printPose(live);
        printf(",\"baseHeight\":%.9g,\"stopAt\":%d,\"ticks\":[", standing.ym, c.stopAt);

        anim::Player player(*config, kin);
        player.play(&c.clip, c.params.data(), (int)c.params.size(), live, standing.ym);
        int idleTicks = 0;
        for (int tick = 0; tick < MAX_TICKS && idleTicks < TICKS_AFTER_IDLE; tick++) {
            if (tick == c.stopAt) player.stop();
            const anim::Pose &pose = player.update(DT);
            float angles[anim::JOINTS];
            const uint32_t mask = anim::poseToAngles(pose, kin, stance, player.base(), angles);
            printf("%s{\"state\":%d,\"t\":%.9g,\"base\":%.9g,\"mask\":%u,\"angles\":[", tick ? "," : "",
                   (int)player.state(), player.t(), player.base(), (unsigned)mask);
            printFloats(angles, anim::JOINTS);
            printf("]}");
            if (player.state() == anim::State::IDLE) idleTicks++;
        }
        printf("]}");
    }
    printf("]}\n");
    return 0;
}
