#pragma once

#include <core_dump.h>
#include <esp_core_dump.h>
#include <esp_log.h>
#include <esp_partition.h>

/** The coredump partition the panic handler writes to, read and erased through esp_core_dump_*. */
class EspCoreDumpStore : public CoreDumpStore {
  public:
    bool hasPartition() override { return partition() != nullptr; }

    uint32_t size() override {
        size_t address = 0, size = 0;
        return esp_core_dump_image_get(&address, &size) == ESP_OK ? size : 0;
    }

    bool summary(CoreDumpSummary &out) override {
        esp_core_dump_summary_t summary;
        if (esp_core_dump_get_summary(&summary) != ESP_OK) return false;
        copyTerminated(out.task, summary.exc_task);
        out.pc = summary.exc_pc;
#if CONFIG_IDF_TARGET_ARCH_RISCV
        out.excCause = summary.ex_info.mcause;
        out.excVaddr = summary.ex_info.mtval;
        out.depth = 0;
        out.riscv = true;
#else
        out.excCause = summary.ex_info.exc_cause;
        out.excVaddr = summary.ex_info.exc_vaddr;
        out.depth = std::min<uint32_t>(summary.exc_bt_info.depth, std::size(out.backtrace));
        std::copy_n(summary.exc_bt_info.bt, out.depth, out.backtrace);
        out.backtraceCorrupted = summary.exc_bt_info.corrupted;
        out.riscv = false;
#endif
        copyTerminated(out.elfSha256,
                       reinterpret_cast<const char(&)[sizeof(summary.app_elf_sha256)]>(summary.app_elf_sha256));
        // Only aborts, asserts and watchdogs leave a reason; an exception is told by its cause.
        if (esp_core_dump_get_panic_reason(out.panicReason, sizeof(out.panicReason)) != ESP_OK)
            out.panicReason[0] = '\0';
        return true;
    }

    // The dump starts at the partition's first byte, header and all, which is what esp-coredump's raw format reads.
    bool read(uint32_t offset, uint8_t *into, size_t size) override {
        const esp_partition_t *core = partition();
        return core && esp_partition_read(core, offset, into, size) == ESP_OK;
    }

    bool erase() override {
        const esp_err_t err = esp_core_dump_image_erase();
        if (err != ESP_OK) ESP_LOGW("CoreDump", "Erase failed: %s", esp_err_to_name(err));
        return err == ESP_OK;
    }

  private:
    static const esp_partition_t *partition() {
        return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
    }
};
