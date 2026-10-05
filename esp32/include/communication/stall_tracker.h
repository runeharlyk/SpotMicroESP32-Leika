#pragma once

#include <cstdint>
#include <map>
#include <mutex>

/**
 * Tells a client that went away from one that is slow: a socket that stays full for longer than the limit belongs to
 * a peer that no longer takes data. TCP alone notices only after minutes of retransmissions, and holds the unsent
 * frames in memory until then.
 */
class StallTracker {
  public:
    explicit StallTracker(uint32_t limitMs) : limitMs_(limitMs) {}

    /** Records whether the client's socket could take a frame now; true once it has been full for over the limit. */
    bool gone(int cid, bool writable, uint32_t nowMs) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (writable) {
            fullSince_.erase(cid);
            return false;
        }
        const auto since = fullSince_.emplace(cid, nowMs).first->second;
        return nowMs - since > limitMs_;
    }

    void forget(int cid) {
        std::lock_guard<std::mutex> lock(mutex_);
        fullSince_.erase(cid);
    }

  private:
    const uint32_t limitMs_;
    std::mutex mutex_;
    std::map<int, uint32_t> fullSince_;
};
