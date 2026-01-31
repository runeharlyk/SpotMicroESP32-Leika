#pragma once
#include <vector>
#include <functional>
#include <type_traits>
#include <cstddef>
#include <cstring>
#include <array>
#include <optional>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

template <typename Sig, size_t MaxSize>
class FixedFn;

template <typename R, typename... A, size_t MaxSize>
class FixedFn<R(A...), MaxSize> {
    alignas(void*) std::byte buf[MaxSize];
    R (*call)(void*, A&&...) {};
    void (*moveFn)(void*, void*) {};
    void (*destroy)(void*) {};

  public:
    template <typename Fun>
    void set(Fun&& f) {
        static_assert(sizeof(Fun) <= MaxSize);
        new (buf) Fun(std::forward<Fun>(f));
        call = [](void* p, A&&... as) -> R { return (*reinterpret_cast<Fun*>(p))(std::forward<A>(as)...); };
        moveFn = [](void* d, void* s) { new (d) Fun(std::move(*reinterpret_cast<Fun*>(s))); };
        destroy = [](void* p) { reinterpret_cast<Fun*>(p)->~Fun(); };
    }
    R operator()(A... as) const {
        return call(const_cast<void*>(static_cast<const void*>(buf)), std::forward<A>(as)...);
    }
    FixedFn() = default;
    FixedFn(FixedFn&& o) noexcept {
        if (o.moveFn) o.moveFn(buf, o.buf);
        call = o.call;
        moveFn = o.moveFn;
        destroy = o.destroy;
        o.call = nullptr;
        o.moveFn = nullptr;
        o.destroy = nullptr;
    }
    FixedFn(const FixedFn&) = delete;
    FixedFn& operator=(const FixedFn&) = delete;
    FixedFn& operator=(FixedFn&&) = delete;
    ~FixedFn() {
        if (destroy) destroy(buf);
    }
};

enum class EmitMode { Latest, Batch };

template <typename Msg, size_t QueueDepth = 64, size_t MaxSubs = 8, size_t BatchSize = 16>
class TypedEventBus {
    struct Item {
        Msg payload;
        size_t exclude;
    };
    static constexpr size_t NO_EX = MaxSubs;
    struct Sub {
        FixedFn<void(const Msg*, size_t), 48> cb;
        TickType_t interval;
        TickType_t last;
        EmitMode mode;
        std::array<Msg, BatchSize> buf;
        size_t cnt;
        std::atomic<bool> enabled;
        std::atomic<uint32_t> running;
    };
    inline static StaticQueue_t qbuf;
    inline static Item qStorage[QueueDepth];
    inline static QueueHandle_t queue =
        xQueueCreateStatic(QueueDepth, sizeof(Item), reinterpret_cast<uint8_t*>(qStorage), &qbuf);
    inline static std::array<std::optional<Sub>, MaxSubs> subs {};
    inline static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    inline static Msg latest {};
    inline static std::atomic<bool> hasLatest {false};
    inline static std::atomic<size_t> subCount {0};
    inline static std::atomic<bool> taskStarted {false};

    static void storeISR(const Msg& m) {
        UBaseType_t s = portSET_INTERRUPT_MASK_FROM_ISR();
        latest = m;
        hasLatest.store(true, std::memory_order_release);
        portCLEAR_INTERRUPT_MASK_FROM_ISR(s);
    }

    static void dispatch(const Msg& m, size_t ex) {
        TickType_t now = xTaskGetTickCount();
        Sub* ready[MaxSubs];
        size_t readyCnt = 0;

        portENTER_CRITICAL(&mux);
        for (size_t i = 0; i < MaxSubs; ++i) {
            auto& opt = subs[i];
            if (!opt || i == ex) continue;
            Sub& s = *opt;
            if (!s.enabled.load(std::memory_order_acquire)) continue;

            TickType_t dt = now - s.last;

            if (s.interval && dt < s.interval) {
                if (s.mode == EmitMode::Batch) {
                    if (s.cnt < BatchSize)
                        s.buf[s.cnt++] = m;
                    else
                        s.buf[BatchSize - 1] = m;
                } else {
                    s.buf[0] = m;
                    s.cnt = 1;
                }
            } else {
                if (s.cnt < BatchSize)
                    s.buf[s.cnt++] = m;
                else
                    s.buf[BatchSize - 1] = m;
                s.last = now;
                s.running.fetch_add(1, std::memory_order_acq_rel);
                ready[readyCnt++] = &s;
            }
        }
        portEXIT_CRITICAL(&mux);

        for (size_t i = 0; i < readyCnt; ++i) {
            Sub* s = ready[i];
            s->cb(s->buf.data(), s->cnt);
            s->cnt = 0;
            s->running.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    static void worker(void*) {
        Item it;
        while (xQueueReceive(queue, &it, portMAX_DELAY) == pdTRUE) dispatch(it.payload, it.exclude);
    }

    static void ensureTask() {
        if (!taskStarted.load(std::memory_order_acquire)) {
            bool expected = false;
            if (taskStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                xTaskCreatePinnedToCore(worker, "evtbus", 4096, nullptr, 6, nullptr, 1);
            }
        }
    }

    static bool push(const Msg& m, size_t ex = NO_EX, TickType_t to = 0) {
        ensureTask();
        Item it {m, ex};
        return xQueueSend(queue, &it, to) == pdTRUE;
    }

  public:
    class Handle {
        size_t idx {NO_EX};
        friend class TypedEventBus;
        explicit Handle(size_t i) : idx(i) {}

      public:
        Handle() = default;
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        Handle(Handle&& o) noexcept : idx(o.idx) { o.idx = NO_EX; }
        Handle& operator=(Handle&& o) noexcept {
            if (this != &o) {
                unsubscribe();
                idx = o.idx;
                o.idx = NO_EX;
            }
            return *this;
        }
        ~Handle() { unsubscribe(); }
        void unsubscribe() {
            if (idx < MaxSubs) {
                Sub* s = nullptr;
                portENTER_CRITICAL(&mux);
                if (subs[idx]) {
                    s = &*subs[idx];
                    s->enabled.store(false, std::memory_order_release);
                }
                portEXIT_CRITICAL(&mux);
                if (s) {
                    while (s->running.load(std::memory_order_acquire) != 0) taskYIELD();
                    portENTER_CRITICAL(&mux);
                    subs[idx].reset();
                    portEXIT_CRITICAL(&mux);
                    subCount.fetch_sub(1, std::memory_order_acq_rel);
                }
                idx = NO_EX;
            }
        }
        bool valid() const { return idx < MaxSubs; }
    };

    static void store(const Msg& m) {
        portENTER_CRITICAL(&mux);
        latest = m;
        hasLatest.store(true, std::memory_order_release);
        portEXIT_CRITICAL(&mux);
    }

    template <typename C>
    static void consume(C fn) {
        static Handle h = subscribe(std::forward<C>(fn));
        (void)h;
    }

    template <typename C>
    static Handle subscribe(uint32_t ms, EmitMode mode, C fn) {
        ensureTask();
        portENTER_CRITICAL(&mux);
        for (size_t i = 0; i < MaxSubs; ++i)
            if (!subs[i]) {
                subs[i].emplace();
                Sub& s = *subs[i];
                s.cb.set(std::move(fn));
                s.interval = pdMS_TO_TICKS(ms);
                s.last = xTaskGetTickCount() - s.interval;
                s.mode = mode;
                s.cnt = 0;
                s.enabled.store(true, std::memory_order_release);
                s.running.store(0, std::memory_order_release);
                subCount.fetch_add(1, std::memory_order_acq_rel);
                portEXIT_CRITICAL(&mux);
                return Handle(i);
            }
        portEXIT_CRITICAL(&mux);
        return Handle(NO_EX);
    }

    template <typename C>
    static Handle subscribe(C fn) {
        if constexpr (std::is_invocable_v<C, const Msg*, size_t>)
            return subscribe(0, EmitMode::Latest, std::move(fn));
        else
            return subscribe(0, EmitMode::Latest, [fn = std::move(fn)](const Msg* p, size_t n) {
                for (size_t i = 0; i < n; ++i) fn(p[i]);
            });
    }

    template <typename C>
    static Handle subscribe(uint32_t ms, C fn) {
        if constexpr (std::is_invocable_v<C, const Msg*, size_t>)
            return subscribe(ms, EmitMode::Batch, std::move(fn));
        else
            return subscribe(ms, EmitMode::Batch, [fn = std::move(fn)](const Msg* p, size_t n) {
                for (size_t i = 0; i < n; ++i) fn(p[i]);
            });
    }

    static bool publish(const Msg& m) {
        store(m);
        return push(m);
    }

    static bool publish(const Msg& m, const Handle& h) {
        store(m);
        return push(m, h.valid() ? h.idx : NO_EX);
    }

    static void publishISR(const Msg& m, BaseType_t* hpw = nullptr) {
        storeISR(m);
        Item it {m, NO_EX};
        xQueueSendFromISR(queue, &it, hpw);
    }

    static bool peek(Msg& out) {
        if (!hasLatest.load(std::memory_order_acquire)) return false;
        portENTER_CRITICAL(&mux);
        out = latest;
        portEXIT_CRITICAL(&mux);
        return true;
    }

    static bool take(Msg& out) {
        if (!hasLatest.load(std::memory_order_acquire)) return false;
        portENTER_CRITICAL(&mux);
        out = latest;
        hasLatest.store(false, std::memory_order_release);
        portEXIT_CRITICAL(&mux);
        return true;
    }

    static bool hasSubscribers() { return subCount.load(std::memory_order_acquire) > 0; }
};
