// Host test of core_dump.h and of the download it streams through filesystem_ws.cpp, built and run by
// test_host_programs.py.
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <vector>
#include <core_dump.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// filesystem_ws.cpp links against these; no test here uploads or makes a directory.
esp_err_t esp_littlefs_info(const char *, size_t *, size_t *) { return ESP_FAIL; }
bool FileSystem::mkdirRecursive(const char *) { return false; }

// A coredump partition in memory; a read can be made to fail from a given offset on.
struct FakeStore : CoreDumpStore {
    bool partition = true;
    std::vector<uint8_t> dump;
    bool checksumHolds = true;
    CoreDumpSummary stored {};
    uint32_t failReadsFrom = UINT32_MAX;
    std::vector<std::pair<uint32_t, size_t>> reads;

    bool hasPartition() override { return partition; }
    uint32_t size() override { return dump.size(); }
    bool summary(CoreDumpSummary &out) override {
        if (!checksumHolds) return false;
        out = stored;
        return true;
    }
    bool read(uint32_t offset, uint8_t *into, size_t size) override {
        reads.emplace_back(offset, size);
        if (offset + size > dump.size() || offset + size > failReadsFrom) return false;
        memcpy(into, dump.data() + offset, size);
        return true;
    }
    bool erase() override {
        dump.clear();
        return true;
    }
};

static std::vector<uint8_t> bytesOf(size_t size) {
    std::vector<uint8_t> bytes(size);
    for (size_t i = 0; i < size; i++) bytes[i] = static_cast<uint8_t>(i * 7 + i / 251);
    return bytes;
}

static CoreDumpSummary aCrash() {
    CoreDumpSummary crash {};
    strcpy(crash.task, "control");
    crash.pc = 0x42001234;
    crash.excCause = 28;
    crash.excVaddr = 0x4;
    crash.depth = 3;
    crash.backtrace[0] = 0x42001234;
    crash.backtrace[1] = 0x42005678;
    crash.backtrace[2] = 0x40379abc;
    memset(crash.elfSha256, 'a', 64);
    strcpy(crash.panicReason, "assert failed: tick motion.cpp:42");
    return crash;
}

static void aTableWithoutThePartitionSaysSo() {
    FakeStore store;
    store.partition = false;
    store.dump = bytesOf(100);
    socket_message_CoreDumpReport report;

    reportCoreDump(store, report);

    CHECK(!report.partition);
    CHECK(!report.stored);
    CHECK(report.size == 0);
}

static void anEmptyPartitionReportsNoCrash() {
    FakeStore store;
    store.stored = aCrash();
    socket_message_CoreDumpReport report;

    reportCoreDump(store, report);

    CHECK(report.partition);
    CHECK(!report.stored);
    CHECK(!report.readable);
    CHECK(report.task[0] == '\0');
}

static void aStoredCrashIsSummarised() {
    FakeStore store;
    store.dump = bytesOf(9000);
    store.stored = aCrash();
    socket_message_CoreDumpReport report;

    reportCoreDump(store, report);

    CHECK(report.stored);
    CHECK(report.readable);
    CHECK(report.size == 9000);
    CHECK(std::string(report.task) == "control");
    CHECK(report.pc == 0x42001234);
    CHECK(report.exc_cause == 28);
    CHECK(report.exc_vaddr == 0x4);
    CHECK(report.backtrace_count == 3);
    CHECK(report.backtrace[2] == 0x40379abc);
    CHECK(std::string(report.app_elf_sha256) == std::string(64, 'a'));
    CHECK(std::string(report.panic_reason) == "assert failed: tick motion.cpp:42");
    CHECK(!report.riscv);
}

// A dump cut short by a reset fails its checksum; the size still tells there is something to download.
static void aCorruptDumpIsStoredButUnread() {
    FakeStore store;
    store.dump = bytesOf(4000);
    store.stored = aCrash();
    store.checksumHolds = false;
    socket_message_CoreDumpReport report;

    reportCoreDump(store, report);

    CHECK(report.stored);
    CHECK(report.size == 4000);
    CHECK(!report.readable);
    CHECK(report.pc == 0);
    CHECK(report.backtrace_count == 0);
}

// The proto holds what a summary can: a depth past the array, or strings without their terminator, stay in bounds.
static void anOverlongSummaryStaysInBounds() {
    FakeStore store;
    store.dump = bytesOf(10);
    store.stored = aCrash();
    store.stored.depth = 1000;
    memset(store.stored.task, 'x', sizeof(store.stored.task));
    memset(store.stored.elfSha256, 'b', sizeof(store.stored.elfSha256));
    memset(store.stored.panicReason, 'r', sizeof(store.stored.panicReason));
    socket_message_CoreDumpReport report;

    reportCoreDump(store, report);

    CHECK(report.backtrace_count == 16);
    CHECK(strlen(report.task) == sizeof(report.task) - 1);
    CHECK(strlen(report.app_elf_sha256) == 64);
    CHECK(strlen(report.panic_reason) == sizeof(report.panic_reason) - 1);
}

static void theSourceReadsTheDumpInOrderAndStopsAtItsEnd() {
    FakeStore store;
    store.dump = bytesOf(1000);
    CoreDumpSource source(store, store.dump.size());
    std::vector<uint8_t> got;
    uint8_t buffer[384];

    for (size_t n; (n = source.read(buffer, sizeof(buffer))) > 0;) got.insert(got.end(), buffer, buffer + n);

    CHECK(got == store.dump);
    CHECK(store.reads.size() == 3);
    CHECK(store.reads.back() == std::make_pair(768u, size_t(232)));
}

// What the handler sends to the one client, and the work it queues for the socket's task.
struct Wire {
    std::vector<socket_message_FSDownloadMetadata> metadata;
    std::vector<std::vector<uint8_t>> chunks;
    std::vector<uint32_t> chunkIndices;
    std::vector<socket_message_FSDownloadComplete> completes;
    std::deque<std::function<void()>> queued;

    void attach(FileSystemWS::FileSystemHandler &handler) {
        handler.setSendCallbacks(
            [this](const socket_message_FSDownloadMetadata &m, int) { metadata.push_back(m); },
            [this](const socket_message_FSDownloadData &d, int) {
                chunks.emplace_back(d.data->bytes, d.data->bytes + d.data->size);
                chunkIndices.push_back(d.chunk_index);
            },
            [this](const socket_message_FSDownloadComplete &c, int) { completes.push_back(c); },
            [](const socket_message_FSUploadComplete &, int) {});
        handler.setScheduling(
            [this](std::function<void()> work) {
                queued.push_back(std::move(work));
                return true;
            },
            [](int, uint32_t) { return true; });
    }

    void drain() {
        while (!queued.empty()) {
            auto work = std::move(queued.front());
            queued.pop_front();
            work();
        }
    }
};

static const int CLIENT = 3;

static void aDumpStreamsAsADownloadInChunks() {
    FakeStore store;
    store.dump = bytesOf(FS_MAX_CHUNK_SIZE * 2 + 7232);
    FileSystemWS::FileSystemHandler handler;
    Wire wire;
    wire.attach(handler);

    handler.startDownload(std::make_unique<CoreDumpSource>(store, store.dump.size()), store.dump.size(), CLIENT);
    wire.drain();

    CHECK(wire.metadata.size() == 1);
    CHECK(wire.metadata[0].success);
    CHECK(wire.metadata[0].file_size == store.dump.size());
    CHECK(wire.metadata[0].total_chunks == 3);
    CHECK((wire.chunkIndices == std::vector<uint32_t> {0, 1, 2}));
    std::vector<uint8_t> received;
    for (const auto &chunk : wire.chunks) received.insert(received.end(), chunk.begin(), chunk.end());
    CHECK(received == store.dump);
    for (const auto &read : store.reads) CHECK(read.second <= FS_MAX_CHUNK_SIZE);
    CHECK(wire.completes.size() == 1);
    CHECK(wire.completes[0].success);
    CHECK(wire.completes[0].transfer_id == wire.metadata[0].transfer_id);
}

static void aFailedFlashReadEndsTheDownloadAsFailed() {
    FakeStore store;
    store.dump = bytesOf(FS_MAX_CHUNK_SIZE * 3);
    store.failReadsFrom = FS_MAX_CHUNK_SIZE + 1;
    FileSystemWS::FileSystemHandler handler;
    Wire wire;
    wire.attach(handler);

    handler.startDownload(std::make_unique<CoreDumpSource>(store, store.dump.size()), store.dump.size(), CLIENT);
    wire.drain();

    CHECK(wire.chunks.size() == 1);
    CHECK(wire.completes.size() == 1);
    CHECK(!wire.completes[0].success);
    CHECK(std::string(wire.completes[0].error) == "Failed to read");
    CHECK(wire.completes[0].total_chunks == 1);
}

// A source the transfer owns, which records its release.
struct CountedSource : FileSystemWS::DownloadSource {
    int &released;
    explicit CountedSource(int &released) : released(released) {}
    ~CountedSource() override { released++; }
    size_t read(uint8_t *into, size_t size) override {
        memset(into, 0, size);
        return size;
    }
};

static void aClientLeavingReleasesItsSourceAndStopsTheStream() {
    FileSystemWS::FileSystemHandler handler;
    Wire wire;
    wire.attach(handler);
    int released = 0;

    handler.startDownload(std::make_unique<CountedSource>(released), FS_MAX_CHUNK_SIZE * 4, CLIENT);
    handler.dropClient(CLIENT);
    wire.drain();

    CHECK(released == 1);
    CHECK(wire.chunks.size() == 1);
    CHECK(wire.completes.empty());
}

static void aRefusalCarriesItsReason() {
    FileSystemWS::FileSystemHandler handler;
    Wire wire;
    wire.attach(handler);

    handler.refuseDownload("No core dump stored", CLIENT);

    CHECK(wire.metadata.size() == 1);
    CHECK(!wire.metadata[0].success);
    CHECK(std::string(wire.metadata[0].error) == "No core dump stored");
    CHECK(wire.queued.empty());
}

int main() {
    aTableWithoutThePartitionSaysSo();
    anEmptyPartitionReportsNoCrash();
    aStoredCrashIsSummarised();
    aCorruptDumpIsStoredButUnread();
    anOverlongSummaryStaysInBounds();
    theSourceReadsTheDumpInOrderAndStopsAtItsEnd();
    aDumpStreamsAsADownloadInChunks();
    aFailedFlashReadEndsTheDownloadAsFailed();
    aClientLeavingReleasesItsSourceAndStopsTheStream();
    aRefusalCarriesItsReason();
    if (failures) std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
