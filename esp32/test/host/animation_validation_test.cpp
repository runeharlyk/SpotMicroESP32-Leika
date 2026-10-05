// Host test of anim::validate against animations/fixtures/validation.json, which the app's unit test reads too, so
// both validators give the same first error; pack_animations.py encodes the cases. Built and run by
// test_host_programs.py.
#include <cstdio>
#include <pb_decode.h>
#include <animation/animation_codec.h>
#include "animation_validation_cases.h"

int main() {
    static animation_Animation message;
    static anim::Clip clip;
    int failures = 0;
    for (const ValidationCase &c : VALIDATION_CASES) {
        message = animation_Animation_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(c.data, c.size);
        if (!pb_decode(&stream, animation_Animation_fields, &message)) {
            std::printf("FAIL %s: does not decode\n", c.name);
            failures++;
            continue;
        }
        anim::fromProto(message, clip);
        const char *error = anim::validate(clip);
        const bool same = error && c.error ? std::strcmp(error, c.error) == 0 : error == c.error;
        if (!same) {
            std::printf("FAIL %s: expected \"%s\", got \"%s\"\n", c.name, c.error ? c.error : "valid",
                        error ? error : "valid");
            failures++;
        }
    }
    if (failures) std::printf("%d case(s) failed\n", failures);
    return failures ? 1 : 0;
}
