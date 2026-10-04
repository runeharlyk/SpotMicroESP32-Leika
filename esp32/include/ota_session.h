#pragma once

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <mutex>

/** The app slot an update is written to: esp_ota_* on the robot (ota_flash.h), a fake in the host tests. */
class OtaFlash {
  public:
    virtual ~OtaFlash() = default;
    /** Bytes the next app slot holds; 0 when the partition table has none. */
    virtual uint32_t capacity() = 0;
    virtual bool begin() = 0;
    virtual bool write(const uint8_t *data, size_t size) = 0;
    /** Closes the slot and validates the image in it; releases the slot whatever it returns. */
    virtual bool end() = 0;
    /** Makes the validated image the one booted next. */
    virtual bool activate() = 0;
    virtual void abort() = 0;
};

/** The status a request is answered with, and on refusal the reason the app shows. */
struct OtaReply {
    uint32_t status;
    const char *reason;
};

/**
 * One firmware update at a time, owned by the client that started it. The image goes into the slot the robot does
 * not run from, in order; the boot partition changes only when all of it arrived and validated, so an update that
 * fails at any point leaves the robot booting what it runs now. Called from the socket's task and, for the timeout,
 * from the service task.
 */
class OtaSession {
  public:
    static constexpr uint32_t TIMEOUT_MS = 30000;

    explicit OtaSession(OtaFlash &flash) : flash_(flash) {}

    OtaReply start(uint32_t size, bool deactivated, int client, uint32_t nowMs) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) return {409, "An update is already running"};
        if (!deactivated) return {409, "Deactivate first"};
        const uint32_t capacity = flash_.capacity();
        if (capacity == 0) return {501, "This partition table has no slot for updates"};
        if (size == 0 || size > capacity) return {413, "The image does not fit the update slot"};
        if (!flash_.begin()) return {500, "Could not open the update slot"};
        running_ = true;
        client_ = client;
        size_ = size;
        written_ = 0;
        nextChunk_ = 0;
        lastActivityMs_ = nowMs;
        return OK;
    }

    OtaReply chunk(uint32_t index, const uint8_t *data, size_t size, int client, uint32_t nowMs) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!owns(client)) return NOT_RUNNING;
        lastActivityMs_ = nowMs;
        if (index != nextChunk_) return abort({400, "A chunk arrived out of order"});
        if (size > size_ - written_) return abort({400, "More data than announced"});
        if (!flash_.write(data, size)) return abort({500, "Writing the update slot failed"});
        written_ += size;
        nextChunk_++;
        return OK;
    }

    OtaReply finish(int client) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!owns(client)) return NOT_RUNNING;
        if (written_ != size_) return abort({400, "The image is incomplete"});
        running_ = false;
        if (!flash_.end()) return {422, "The robot rejected the image"};
        if (!flash_.activate()) return {500, "Could not select the new image"};
        return OK;
    }

    /** Ends the update of a client that left. */
    void drop(int client) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (owns(client)) abort(OK);
    }

    /** Ends an update that fell silent for longer than TIMEOUT_MS. */
    void expire(uint32_t nowMs) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_ && nowMs - lastActivityMs_ > TIMEOUT_MS) abort(OK);
    }

    /** Lock-free, so the control task can ask while a chunk is being written to flash. */
    bool running() const { return running_; }

  private:
    static constexpr OtaReply OK {200, ""};
    static constexpr OtaReply NOT_RUNNING {409, "No update running"};

    bool owns(int client) const { return running_ && client == client_; }

    OtaReply abort(OtaReply reply) {
        flash_.abort();
        running_ = false;
        return reply;
    }

    OtaFlash &flash_;
    std::mutex mutex_;
    std::atomic<bool> running_ = false;
    int client_ = -1;
    uint32_t size_ = 0;
    uint32_t written_ = 0;
    uint32_t nextChunk_ = 0;
    uint32_t lastActivityMs_ = 0;
};
