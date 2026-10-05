// Host test of the clips in animations/, as pack_animations.py embeds them; built and run by test_host_programs.py.
#include <cstdio>
#include <initializer_list>
#include <pb_decode.h>
#include <animation/animation_codec.h>
#include <animation/builtin_clips.h>
#include <variant.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// Every clip shipped in the firmware decodes, validates, and keeps every foot within reach on every variant.
static void everyBuiltinClipPlaysOnEveryVariant() {
    static animation_Animation message;
    static anim::Clip clip;
    for (const anim::BuiltinClip &builtin : anim::BUILTIN_CLIPS) {
        message = animation_Animation_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(builtin.data, builtin.size);
        CHECK(pb_decode(&stream, animation_Animation_fields, &message));
        anim::fromProto(message, clip);
        const char *error = anim::validate(clip);
        if (error) std::printf("%s: %s\n", builtin.name, error);
        CHECK(error == nullptr);
        CHECK(std::strcmp(clip.name, builtin.name) == 0);
        for (KinematicsVariant variant :
             {socket_message_KinematicsVariant_SPOTMICRO_ESP32, socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI,
              socket_message_KinematicsVariant_SPOTMICRO_YERTLE}) {
            const KinConfig &config = *kinConfigFor(variant);
            Kinematics kin(config);
            const uint32_t mask = anim::clampSweep(clip, kin, config.default_feet_positions, config.default_body_height);
            if (mask) std::printf("%s on %s: clamp mask 0x%03x\n", builtin.name, variantName(variant), (unsigned)mask);
            CHECK(mask == 0);
        }
    }
}

int main() {
    everyBuiltinClipPlaysOnEveryVariant();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
