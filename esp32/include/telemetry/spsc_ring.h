#pragma once

#include <array>
#include <atomic>
#include <cstddef>

/**
 * A fixed ring between one producer task and one consumer task, without locks: the producer never waits, and a full
 * ring refuses the item instead.
 */
template <typename T, size_t N>
class SpscRing {
    static_assert(N > 0 && (N & (N - 1)) == 0, "the capacity must be a power of two");

  public:
    bool push(const T &item) {
        const size_t head = _head.load(std::memory_order_relaxed);
        if (head - _tail.load(std::memory_order_acquire) == N) return false;
        _slots[head % N] = item;
        _head.store(head + 1, std::memory_order_release);
        return true;
    }

    bool pop(T &item) {
        const size_t tail = _tail.load(std::memory_order_relaxed);
        if (tail == _head.load(std::memory_order_acquire)) return false;
        item = _slots[tail % N];
        _tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    size_t size() const { return _head.load(std::memory_order_acquire) - _tail.load(std::memory_order_acquire); }

  private:
    std::array<T, N> _slots {};
    std::atomic<size_t> _head {0};
    std::atomic<size_t> _tail {0};
};
