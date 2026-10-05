// Host test of the ProtoDecoder in proto_helpers.h and of the protocol's memory footprint, built and run by
// test_host_programs.py with nanopb's allocations counted (stubs/counting_alloc.h).
#include <cstdio>
#include <cstring>
#include <vector>
#include <communication/proto_helpers.h>

extern "C" int countingAllocLive = 0;

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// An upload chunk as the app sends it, encoded by hand so the test does not depend on the encoder under test.
static std::vector<uint8_t> uploadChunk(uint32_t transferId, const std::vector<uint8_t> &data) {
    std::vector<uint8_t> out(data.size() + 64);
    pb_ostream_t stream = pb_ostream_from_buffer(out.data(), out.size());
    // Message.fs_upload_data (42) holding FSUploadData{transfer_id = 1, chunk_index = 2, data = 3}.
    std::vector<uint8_t> inner(data.size() + 32);
    pb_ostream_t innerStream = pb_ostream_from_buffer(inner.data(), inner.size());
    pb_encode_tag(&innerStream, PB_WT_VARINT, 1);
    pb_encode_varint(&innerStream, transferId);
    pb_encode_tag(&innerStream, PB_WT_VARINT, 2);
    pb_encode_varint(&innerStream, 7);
    pb_encode_tag(&innerStream, PB_WT_STRING, 3);
    pb_encode_string(&innerStream, data.data(), data.size());
    pb_encode_tag(&stream, PB_WT_STRING, socket_message_Message_fs_upload_data_tag);
    pb_encode_string(&stream, inner.data(), innerStream.bytes_written);
    out.resize(stream.bytes_written);
    return out;
}

static void aChunkReachesItsHandlerAndIsFreedAfterwards() {
    ProtoDecoder decoder;
    std::vector<uint8_t> seen;
    uint32_t seenId = 0;
    int liveInHandler = -1;
    decoder.on<socket_message_FSUploadData>([&](const socket_message_FSUploadData &chunk, int) {
        seenId = chunk.transfer_id;
        if (chunk.data) seen.assign(chunk.data->bytes, chunk.data->bytes + chunk.data->size);
        liveInHandler = countingAllocLive;
    });

    std::vector<uint8_t> data(16384);
    for (size_t i = 0; i < data.size(); i++) data[i] = static_cast<uint8_t>(i * 7);
    const std::vector<uint8_t> frame = uploadChunk(42, data);

    CHECK(decoder.decode(frame.data(), frame.size(), 1));
    CHECK(seenId == 42);
    CHECK(seen == data);
    CHECK(liveInHandler > 0);
    CHECK(countingAllocLive == 0);

    // A second chunk through the same decoder neither leaks nor sees the first one's bytes.
    const std::vector<uint8_t> small = {1, 2, 3};
    const std::vector<uint8_t> second = uploadChunk(43, small);
    CHECK(decoder.decode(second.data(), second.size(), 1));
    CHECK(seen == small);
    CHECK(countingAllocLive == 0);
}

static void aChunkWithoutHandlerIsFreedToo() {
    ProtoDecoder decoder;
    const std::vector<uint8_t> frame = uploadChunk(1, std::vector<uint8_t>(100, 9));
    CHECK(!decoder.decode(frame.data(), frame.size(), 1));
    CHECK(countingAllocLive == 0);
}

static void aTruncatedFrameLeavesNothingAllocated() {
    ProtoDecoder decoder;
    decoder.on<socket_message_FSUploadData>([](const socket_message_FSUploadData &, int) {});
    std::vector<uint8_t> frame = uploadChunk(1, std::vector<uint8_t>(1000, 5));
    frame.resize(frame.size() - 10);
    CHECK(!decoder.decode(frame.data(), frame.size(), 1));
    CHECK(countingAllocLive == 0);
}

// Every link keeps two Messages and every request allocates a CorrelationResponse; on a board without PSRAM both
// must stay small, or the heap runs out (the 16 KB file chunks used to sit inline in both).
static void theProtocolStructsStaySmall() {
    CHECK(sizeof(socket_message_Message) <= 4096);
    CHECK(sizeof(socket_message_CorrelationResponse) <= 2048);
}

int main() {
    aChunkReachesItsHandlerAndIsFreedAfterwards();
    aChunkWithoutHandlerIsFreedToo();
    aTruncatedFrameLeavesNothingAllocated();
    theProtocolStructsStaySmall();
    if (failures) std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
