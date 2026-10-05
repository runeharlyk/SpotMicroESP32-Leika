// Host test of animation/animation_codec.h, built and run by test_host_programs.py.
#include <cmath>
#include <cstdio>
#include <animation/animation_codec.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void aDecodedClipCarriesEveryField() {
    animation_Animation m = animation_Animation_init_zero;
    std::strcpy(m.name, "sit");
    m.schema = 1;
    m.hold_end = true;
    m.entry_time = 0.8f;
    m.has_ride_height = true;
    m.ride_height = 60;
    m.keyframes_count = 2;
    m.keyframes[1].time = 1;
    m.keyframes[1].ease = animation_Ease_EASE_OUT;
    m.keyframes[1].has_body = true;
    m.keyframes[1].body.pitch = -0.2f;
    m.keyframes[1].body.z = -15;
    m.keyframes[1].legs_count = 4;
    m.keyframes[1].legs[2].which_target = animation_LegTarget_foot_tag;
    m.keyframes[1].legs[2].target.foot.x = -10;
    m.keyframes[1].legs[3].which_target = animation_LegTarget_joints_tag;
    m.keyframes[1].legs[3].target.joints.tibia = 95;
    m.overlays_count = 1;
    m.overlays[0].which_channel = animation_Overlay_foot_channel_tag;
    m.overlays[0].channel.foot_channel = 11;
    m.overlays[0].end = 1;
    m.params_count = 1;
    m.params[0] = {animation_ParamId_SPEED, 0.5f, 1, 2};

    anim::Clip c;
    anim::fromProto(m, c);
    CHECK(std::strcmp(c.name, "sit") == 0 && c.holdEnd && c.entryTime == 0.8f);
    CHECK(c.hasRideHeight && c.rideHeight == 60);
    CHECK(c.keyframeCount == 2 && c.keyframes[1].ease == anim::EASE_OUT);
    CHECK(c.keyframes[1].body[anim::PITCH] == -0.2f && c.keyframes[1].body[anim::Z] == -15);
    CHECK(c.keyframes[1].legCount == 4 && !c.keyframes[1].legs[2].joints && c.keyframes[1].legs[2].v[0] == -10);
    CHECK(c.keyframes[1].legs[3].joints && c.keyframes[1].legs[3].v[2] == 95);
    CHECK(c.overlayCount == 1 && c.overlays[0].kind == anim::CHANNEL_FOOT && c.overlays[0].channel == 11);
    CHECK(c.paramCount == 1 && c.params[0].id == anim::SPEED && c.params[0].min == 0.5f);
    CHECK(anim::validate(c) == nullptr);
}

int main() {
    aDecodedClipCarriesEveryField();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
