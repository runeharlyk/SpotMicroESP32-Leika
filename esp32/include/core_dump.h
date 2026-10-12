#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <filesystem_ws.h>
#include <platform_shared/message.pb.h>

/** What esp_core_dump_get_summary reads from a dump, without the IDF's types. */
struct CoreDumpSummary {
    char task[16];
    uint32_t pc;
    uint32_t excCause;
    uint32_t excVaddr;
    uint32_t backtrace[16];
    uint32_t depth;
    bool backtraceCorrupted;
    char elfSha256[65];
    char panicReason[128];
    bool riscv;
};

/** The coredump partition: the IDF's on the robot (core_dump_flash.h), a buffer in the host tests. */
class CoreDumpStore {
  public:
    virtual ~CoreDumpStore() = default;
    virtual bool hasPartition() = 0;
    /** Bytes of the stored dump; 0 when none is stored. */
    virtual uint32_t size() = 0;
    /** False when the dump fails its checksum, as one cut short by a reset does. */
    virtual bool summary(CoreDumpSummary &out) = 0;
    virtual bool read(uint32_t offset, uint8_t *into, size_t size) = 0;
    virtual bool erase() = 0;
};

template <size_t N, size_t M>
inline void copyTerminated(char (&into)[N], const char (&from)[M]) {
    const size_t length = strnlen(from, std::min(N - 1, M));
    memcpy(into, from, length);
    into[length] = '\0';
}

inline void reportCoreDump(CoreDumpStore &store, socket_message_CoreDumpReport &report) {
    report = socket_message_CoreDumpReport_init_zero;
    report.partition = store.hasPartition();
    if (!report.partition) return;
    report.size = store.size();
    report.stored = report.size > 0;
    if (!report.stored) return;

    CoreDumpSummary summary {};
    report.readable = store.summary(summary);
    if (!report.readable) return;
    copyTerminated(report.task, summary.task);
    report.pc = summary.pc;
    report.exc_cause = summary.excCause;
    report.exc_vaddr = summary.excVaddr;
    report.backtrace_count = std::min<uint32_t>(summary.depth, std::size(report.backtrace));
    std::copy_n(summary.backtrace, report.backtrace_count, report.backtrace);
    report.backtrace_corrupted = summary.backtraceCorrupted;
    copyTerminated(report.app_elf_sha256, summary.elfSha256);
    copyTerminated(report.panic_reason, summary.panicReason);
    report.riscv = summary.riscv;
}

/** The stored dump as a download, read from flash a chunk at a time: a board without PSRAM cannot hold all of it. */
class CoreDumpSource : public FileSystemWS::DownloadSource {
  public:
    CoreDumpSource(CoreDumpStore &store, uint32_t size) : store_(store), size_(size) {}

    size_t read(uint8_t *into, size_t size) override {
        const size_t length = std::min<size_t>(size, size_ - offset_);
        if (length == 0 || !store_.read(offset_, into, length)) return 0;
        offset_ += length;
        return length;
    }

  private:
    CoreDumpStore &store_;
    const uint32_t size_;
    uint32_t offset_ = 0;
};
