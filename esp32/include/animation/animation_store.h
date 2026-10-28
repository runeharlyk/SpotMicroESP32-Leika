#pragma once

#include <animation/animation_codec.h>
#include <animation/builtin_clips.h>
#include <pb_decode.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace anim {

/**
 * Finds clips by name: the built-ins embedded in the firmware first, then `<name>.pb` files in a directory, uploaded
 * with the file transfer. Runs on the socket's task, which loads a clip before posting it to the control task.
 */
class AnimationStore {
  public:
    explicit AnimationStore(std::string directory) : directory_(std::move(directory)) {}

    /** The clip, decoded and validated; nullptr with `error` set when there is none or it is broken. */
    std::shared_ptr<const Clip> load(const char *name, const char *&error) const {
        if (!validName(name)) {
            error = "not a clip name";
            return nullptr;
        }
        for (const BuiltinClip &builtin : BUILTIN_CLIPS)
            if (std::strcmp(builtin.name, name) == 0) return decode(builtin.data, builtin.size, name, error);
        const std::string path = directory_ + "/" + name + ".pb";
        FILE *file = std::fopen(path.c_str(), "rb");
        if (!file) {
            error = "no such clip";
            return nullptr;
        }
        std::vector<uint8_t> data;
        uint8_t chunk[512];
        size_t n;
        while ((n = std::fread(chunk, 1, sizeof(chunk), file)) > 0) data.insert(data.end(), chunk, chunk + n);
        std::fclose(file);
        return decode(data.data(), data.size(), name, error);
    }

    struct Entry {
        std::string name;
        bool builtin;
        uint32_t size;
    };

    /** The built-ins, then the clip files; a file named like a built-in is shadowed and left out. */
    std::vector<Entry> list() const {
        std::vector<Entry> entries;
        for (const BuiltinClip &builtin : BUILTIN_CLIPS) entries.push_back({builtin.name, true, (uint32_t)builtin.size});
        DIR *dir = opendir(directory_.c_str());
        if (!dir) return entries;
        while (dirent *entry = readdir(dir)) {
            const std::string file = entry->d_name;
            if (file.size() <= 3 || file.compare(file.size() - 3, 3, ".pb") != 0) continue;
            const std::string name = file.substr(0, file.size() - 3);
            bool shadowed = false;
            for (const BuiltinClip &builtin : BUILTIN_CLIPS) shadowed |= name == builtin.name;
            struct stat st;
            if (shadowed || stat((directory_ + "/" + file).c_str(), &st) != 0) continue;
            entries.push_back({name, false, (uint32_t)st.st_size});
        }
        closedir(dir);
        return entries;
    }

  private:
    std::string directory_;

    static std::shared_ptr<const Clip> decode(const uint8_t *data, size_t size, const char *name, const char *&error) {
        // Both are kilobytes: too big for the socket task's stack.
        std::unique_ptr<animation_Animation> message(new (std::nothrow) animation_Animation);
        std::shared_ptr<Clip> clip(new (std::nothrow) Clip);
        if (!message || !clip) {
            error = "no memory for the clip";
            return nullptr;
        }
        *message = animation_Animation_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(data, size);
        if (!pb_decode(&stream, animation_Animation_fields, message.get())) {
            error = "not a clip file";
            return nullptr;
        }
        fromProto(*message, *clip);
        if ((error = validate(*clip))) return nullptr;
        if (std::strcmp(clip->name, name) != 0) {
            error = "the clip inside has another name";
            return nullptr;
        }
        return clip;
    }
};

}  // namespace anim
