// Host test of the BLE framing against platform_shared/ble_frame_vectors.json, which the app's framing passes too;
// built and run by test_host_programs.py, which passes the vectors file's path.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <communication/ble_frame.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

using Bytes = std::vector<uint8_t>;

struct Vector {
    std::string name;
    size_t chunkSize = 0; // 0: the chunks are not an encoder's output
    std::vector<Bytes> chunks;
    std::vector<Bytes> frames;
};

static Bytes fromHex(const std::string &hex) {
    Bytes bytes;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) bytes.push_back(std::stoi(hex.substr(i, 2), nullptr, 16));
    return bytes;
}

static std::vector<Bytes> hexList(const std::string &list) {
    std::vector<Bytes> out;
    for (size_t open = list.find('"'); open != std::string::npos; open = list.find('"', open + 1)) {
        const size_t close = list.find('"', open + 1);
        out.push_back(fromHex(list.substr(open + 1, close - open - 1)));
        open = close;
    }
    return out;
}

// The text between `open` and the matching `close` that follows `key`, from `at` on; `at` moves past it.
static std::string after(const std::string &json, const std::string &key, char open, char close, size_t &at) {
    const size_t start = json.find(open, json.find(key, at) + key.size());
    const size_t end = json.find(close, start + 1);
    at = end + 1;
    return json.substr(start + 1, end - start - 1);
}

// The file is written by a generator, one vector after another with the keys in one order and no escapes; this reads
// only that.
static std::vector<Vector> readVectors(const char *path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    const std::string json = text.str();
    std::vector<Vector> vectors;
    for (size_t at = json.find("\"vectors\""); json.find("\"name\":", at) != std::string::npos;) {
        Vector v;
        v.name = after(json, "\"name\":", '"', '"', at);
        at = json.find("\"chunk_size\":", at) + 13;
        const std::string size = json.substr(at, json.find(',', at) - at);
        if (size.find("null") == std::string::npos) v.chunkSize = std::stoul(size);
        v.chunks = hexList(after(json, "\"chunks\":", '[', ']', at));
        v.frames = hexList(after(json, "\"frames\":", '[', ']', at));
        vectors.push_back(v);
    }
    return vectors;
}

static std::vector<Bytes> decode(const std::vector<Bytes> &chunks) {
    std::vector<Bytes> frames;
    ble_frame::Decoder decoder([&](const uint8_t *m, size_t n) { frames.emplace_back(m, m + n); });
    for (const Bytes &chunk : chunks) decoder.feed(chunk.data(), chunk.size());
    return frames;
}

static void everyVectorDecodesToItsFrames(const std::vector<Vector> &vectors) {
    CHECK(vectors.size() >= 12);
    for (const Vector &v : vectors) {
        const std::vector<Bytes> frames = decode(v.chunks);
        if (frames != v.frames) std::printf("  vector '%s'\n", v.name.c_str());
        CHECK(frames == v.frames);
    }
}

static void everyEncodedVectorIsTheEncodersOutput(const std::vector<Vector> &vectors) {
    int encoded = 0;
    for (const Vector &v : vectors) {
        if (!v.chunkSize) continue;
        encoded++;
        CHECK(v.frames.size() == 1);
        const auto chunks = ble_frame::encode(v.frames[0].data(), v.frames[0].size(), v.chunkSize);
        if (chunks != v.chunks) std::printf("  vector '%s'\n", v.name.c_str());
        CHECK(chunks == v.chunks);
    }
    CHECK(encoded >= 7);
}

static void anEmptyOrOversizedMessageIsNotEncoded() {
    const Bytes big(ble_frame::MAX_MESSAGE + 1, 1);
    CHECK(ble_frame::encode(big.data(), big.size(), 244).empty());
    CHECK(ble_frame::encode(big.data(), 0, 244).empty());
}

// A connection that ended mid-message must not glue its tail onto the next connection's first message.
static void aResetForgetsAPartialMessage() {
    std::vector<Bytes> frames;
    ble_frame::Decoder decoder([&](const uint8_t *m, size_t n) { frames.emplace_back(m, m + n); });
    const Bytes partial = {4, 0, 1, 2}, whole = {2, 0, 9, 9};
    decoder.feed(partial.data(), partial.size());
    decoder.reset();
    decoder.feed(whole.data(), whole.size());
    CHECK((frames == std::vector<Bytes> {{9, 9}}));
}

int main(int argc, char **argv) {
    const std::vector<Vector> vectors = readVectors(argc > 1 ? argv[1] : "ble_frame_vectors.json");
    everyVectorDecodesToItsFrames(vectors);
    everyEncodedVectorIsTheEncodersOutput(vectors);
    anEmptyOrOversizedMessageIsNotEncoded();
    aResetForgetsAPartialMessage();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
