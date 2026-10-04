// Host test of the serial framing against platform_shared/serial_frame_vectors.json, which the app's framing passes
// too; built and run by test_host_programs.py, which passes the vectors file's path.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <communication/serial_frame.h>

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
    Bytes stream;
    std::vector<Bytes> frames;
    bool compareLines = false;
    std::vector<std::string> lines;
};

static Bytes fromHex(const std::string &hex) {
    Bytes bytes;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) bytes.push_back(std::stoi(hex.substr(i, 2), nullptr, 16));
    return bytes;
}

static std::vector<std::string> strings(const std::string &list) {
    std::vector<std::string> out;
    for (size_t open = list.find('"'); open != std::string::npos; open = list.find('"', open + 1)) {
        const size_t close = list.find('"', open + 1);
        out.push_back(list.substr(open + 1, close - open - 1));
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
// only that. (std::regex recurses per character and overflows the stack on the long hex strings.)
static std::vector<Vector> readVectors(const char *path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    const std::string json = text.str();
    std::vector<Vector> vectors;
    for (size_t at = json.find("\"vectors\""); json.find("\"name\":", at) != std::string::npos;) {
        Vector v;
        v.name = after(json, "\"name\":", '"', '"', at);
        v.stream = fromHex(after(json, "\"stream\":", '"', '"', at));
        for (const std::string &hex : strings(after(json, "\"frames\":", '[', ']', at))) v.frames.push_back(fromHex(hex));
        const size_t lines = json.find("\"lines\":", at) + 8;
        v.compareLines = json.compare(json.find_first_not_of(' ', lines), 4, "null") != 0;
        if (v.compareLines) v.lines = strings(after(json, "\"lines\":", '[', ']', at));
        else at = lines + 5;
        vectors.push_back(v);
    }
    return vectors;
}

struct Decoded {
    std::vector<Bytes> frames;
    std::vector<std::string> lines;
};

static Decoded decode(const Bytes &stream, size_t chunk) {
    Decoded out;
    serial_frame::Decoder decoder([&](const uint8_t *m, size_t n) { out.frames.emplace_back(m, m + n); },
                                  [&](const std::string &line) { out.lines.push_back(line); });
    for (size_t i = 0; i < stream.size(); i += chunk) decoder.feed(stream.data() + i, std::min(chunk, stream.size() - i));
    return out;
}

static void everyVectorDecodesAlikeInAnyChunking(const std::vector<Vector> &vectors) {
    CHECK(vectors.size() >= 7);
    for (const Vector &v : vectors) {
        for (size_t chunk : {v.stream.size(), size_t(1), size_t(7)}) {
            const Decoded d = decode(v.stream, chunk);
            if (d.frames != v.frames) std::printf("  vector '%s', chunks of %zu\n", v.name.c_str(), chunk);
            CHECK(d.frames == v.frames);
            if (v.compareLines) CHECK(d.lines == v.lines);
        }
    }
}

static void aSingleFrameEncodesToItsVector(const std::vector<Vector> &vectors) {
    for (const Vector &v : vectors) {
        if (v.frames.size() != 1 || !v.compareLines || !v.lines.empty()) continue;
        CHECK(serial_frame::encode(v.frames[0].data(), v.frames[0].size()) == v.stream);
    }
}

// A port opened mid-frame first sees the frame's tail; the frames after it must all arrive.
static void aReaderStartingMidFrameFindsTheNextFrames() {
    const Bytes a = {8, 1, 0, 0, 7}, b = {0x10, 0x20};
    const Bytes first = serial_frame::encode(a.data(), a.size()), second = serial_frame::encode(b.data(), b.size());
    Bytes stream(first.begin() + first.size() / 2, first.end());
    for (char c : std::string("I (5) wifi: connecting\n")) stream.push_back(c);
    stream.insert(stream.end(), second.begin(), second.end());
    stream.insert(stream.end(), first.begin(), first.end());
    const Decoded d = decode(stream, 3);
    CHECK((d.frames == std::vector<Bytes> {b, a}));
}

static void aMessageOverTheLimitIsNotEncoded() {
    const Bytes big(serial_frame::MAX_MESSAGE + 1, 1);
    CHECK(serial_frame::encode(big.data(), big.size()).empty());
}

// Log text with no frame for a long time must still reach the console, not wait in the frame buffer.
static void longLogTextAfterAStrayZeroStillArrives() {
    Bytes stream = {0};
    const std::string line(200, 'x');
    for (int i = 0; i < 30; i++) {
        for (char c : line) stream.push_back(c);
        stream.push_back('\n');
    }
    const Decoded d = decode(stream, 64);
    CHECK(d.lines.size() >= 25);
}

int main(int argc, char **argv) {
    const std::vector<Vector> vectors = readVectors(argc > 1 ? argv[1] : "serial_frame_vectors.json");
    everyVectorDecodesAlikeInAnyChunking(vectors);
    aSingleFrameEncodesToItsVector(vectors);
    aReaderStartingMidFrameFindsTheNextFrames();
    aMessageOverTheLimitIsNotEncoded();
    longLogTextAfterAStrayZeroStillArrives();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
