// Host test of animation/animation_store.h against a temporary clip directory, built and run by test_host_programs.py.
#include <cstdio>
#include <filesystem>
#include <pb_encode.h>
#include <animation/animation_store.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void writeClip(const std::filesystem::path &path, const char *name, uint32_t schema = 1) {
    animation_Animation m = animation_Animation_init_zero;
    std::strcpy(m.name, name);
    m.schema = schema;
    m.keyframes_count = 1;
    uint8_t buffer[256];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    pb_encode(&stream, animation_Animation_fields, &m);
    FILE *file = std::fopen(path.string().c_str(), "wb");
    std::fwrite(buffer, 1, stream.bytes_written, file);
    std::fclose(file);
}

static void findsBuiltinsAndFilesAndSaysWhyNot() {
    const auto dir = std::filesystem::temp_directory_path() / "leika-animations";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    writeClip(dir / "nod.pb", "nod");
    writeClip(dir / "old.pb", "old", 2);
    writeClip(dir / "renamed.pb", "other");
    writeClip(dir / "sit.pb", "sit");
    anim::AnimationStore store(dir.string());

    const char *error = nullptr;
    auto sit = store.load("sit", error);
    CHECK(sit && sit->holdEnd);  // the built-in, not the file of the same name
    auto nod = store.load("nod", error);
    CHECK(nod && std::strcmp(nod->name, "nod") == 0);

    CHECK(!store.load("missing", error) && std::strcmp(error, "no such clip") == 0);
    CHECK(!store.load("old", error) && std::strcmp(error, "schema is not 1") == 0);
    CHECK(!store.load("renamed", error) && std::strcmp(error, "the clip inside has another name") == 0);
    CHECK(!store.load("../config/x", error) && std::strcmp(error, "not a clip name") == 0);

    int builtins = 0, files = 0, sits = 0;
    for (const auto &entry : store.list()) {
        (entry.builtin ? builtins : files)++;
        sits += entry.name == "sit";
        CHECK(entry.size > 0);
    }
    CHECK(builtins == (int)(sizeof(anim::BUILTIN_CLIPS) / sizeof(anim::BUILTIN_CLIPS[0])));
    CHECK(files == 3);
    CHECK(sits == 1);
    std::filesystem::remove_all(dir);
}

int main() {
    findsBuiltinsAndFilesAndSaysWhyNot();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
