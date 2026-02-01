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
#include <esp_heap_caps.h>

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
        Msg* buf;
        size_t cnt;
        std::atomic<bool> enabled;
        std::atomic<uint32_t> running;

        Sub() : buf(nullptr), cnt(0), enabled(false), running(0) {
            buf = static_cast<Msg*>(heap_caps_malloc(BatchSize * sizeof(Msg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!buf) buf = static_cast<Msg*>(malloc(BatchSize * sizeof(Msg)));
        }
        ~Sub() {
            if (buf) free(buf);
        }
        Sub(const Sub&) = delete;
        Sub& operator=(const Sub&) = delete;
    };

    struct BusState {
        QueueHandle_t queue;
        Sub* subs[MaxSubs];
        portMUX_TYPE mux;
        Msg latest;
        std::atomic<bool> hasLatest;
        std::atomic<size_t> subCount;
        std::atomic<bool> taskStarted;

        BusState()
            : queue(nullptr),
              mux(portMUX_INITIALIZER_UNLOCKED),
              latest {},
              hasLatest(false),
              subCount(0),
              taskStarted(false) {
            for (size_t i = 0; i < MaxSubs; ++i) subs[i] = nullptr;
        }
    };

    static BusState& state() {
        static BusState s;
        return s;
    }

    static void ensureQueue() {
        auto& s = state();
        if (s.queue) return;
        portENTER_CRITICAL(&s.mux);
        if (!s.queue) {
            s.queue = xQueueCreate(QueueDepth, sizeof(Item));
        }
        portEXIT_CRITICAL(&s.mux);
    }

    static void storeISR(const Msg& m) {
        auto& s = state();
        UBaseType_t saved = portSET_INTERRUPT_MASK_FROM_ISR();
        s.latest = m;
        s.hasLatest.store(true, std::memory_order_release);
        portCLEAR_INTERRUPT_MASK_FROM_ISR(saved);
    }

    static void dispatch(const Msg& m, size_t ex) {
        auto& s = state();
        TickType_t now = xTaskGetTickCount();
        Sub* ready[MaxSubs];
        size_t readyCnt = 0;

        portENTER_CRITICAL(&s.mux);
        for (size_t i = 0; i < MaxSubs; ++i) {
            Sub* sub = s.subs[i];
            if (!sub || i == ex) continue;
            if (!sub->enabled.load(std::memory_order_acquire)) continue;

            TickType_t dt = now - sub->last;

            if (sub->interval && dt < sub->interval) {
                if (sub->mode == EmitMode::Batch) {
                    if (sub->cnt < BatchSize)
                        sub->buf[sub->cnt++] = m;
                    else
                        sub->buf[BatchSize - 1] = m;
                } else {
                    sub->buf[0] = m;
                    sub->cnt = 1;
                }
            } else {
                if (sub->cnt < BatchSize)
                    sub->buf[sub->cnt++] = m;
                else
                    sub->buf[BatchSize - 1] = m;
                sub->last = now;
                sub->running.fetch_add(1, std::memory_order_acq_rel);
                ready[readyCnt++] = sub;
            }
        }
        portEXIT_CRITICAL(&s.mux);

        for (size_t i = 0; i < readyCnt; ++i) {
            Sub* sub = ready[i];
            sub->cb(sub->buf, sub->cnt);
            sub->cnt = 0;
            sub->running.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    static void worker(void*) {
        auto& s = state();
        Item it;
        while (xQueueReceive(s.queue, &it, portMAX_DELAY) == pdTRUE) dispatch(it.payload, it.exclude);
    }

    static void ensureTask() {
        auto& s = state();
        if (!s.taskStarted.load(std::memory_order_acquire)) {
            bool expected = false;
            if (s.taskStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                ensureQueue();
                xTaskCreatePinnedToCore(worker, "evtbus", 4096, nullptr, 6, nullptr, 1);
            }
        }
    }

    static bool push(const Msg& m, size_t ex = NO_EX, TickType_t to = 0) {
        ensureTask();
        auto& s = state();
        Item it {m, ex};
        return xQueueSend(s.queue, &it, to) == pdTRUE;
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
                auto& s = state();
                Sub* sub = nullptr;
                portENTER_CRITICAL(&s.mux);
                sub = s.subs[idx];
                if (sub) {
                    sub->enabled.store(false, std::memory_order_release);
                }
                portEXIT_CRITICAL(&s.mux);
                if (sub) {
                    while (sub->running.load(std::memory_order_acquire) != 0) taskYIELD();
                    portENTER_CRITICAL(&s.mux);
                    s.subs[idx] = nullptr;
                    portEXIT_CRITICAL(&s.mux);
                    delete sub;
                    s.subCount.fetch_sub(1, std::memory_order_acq_rel);
                }
                idx = NO_EX;
            }
        }
        bool valid() const { return idx < MaxSubs; }
    };

    static void store(const Msg& m) {
        auto& s = state();
        portENTER_CRITICAL(&s.mux);
        s.latest = m;
        s.hasLatest.store(true, std::memory_order_release);
        portEXIT_CRITICAL(&s.mux);
    }

    template <typename C>
    static void consume(C fn) {
        static Handle h = subscribe(std::forward<C>(fn));
        (void)h;
    }

    template <typename C>
    static Handle subscribe(uint32_t ms, EmitMode mode, C fn) {
        ensureTask();
        auto& s = state();
        portENTER_CRITICAL(&s.mux);
        for (size_t i = 0; i < MaxSubs; ++i) {
            if (!s.subs[i]) {
                Sub* sub = new Sub();
                sub->cb.set(std::move(fn));
                sub->interval = pdMS_TO_TICKS(ms);
                sub->last = xTaskGetTickCount() - sub->interval;
                sub->mode = mode;
                sub->cnt = 0;
                sub->enabled.store(true, std::memory_order_release);
                sub->running.store(0, std::memory_order_release);
                s.subs[i] = sub;
                s.subCount.fetch_add(1, std::memory_order_acq_rel);
                portEXIT_CRITICAL(&s.mux);
                return Handle(i);
            }
        }
        portEXIT_CRITICAL(&s.mux);
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
        auto& s = state();
        storeISR(m);
        ensureQueue();
        Item it {m, NO_EX};
        xQueueSendFromISR(s.queue, &it, hpw);
    }

    static bool peek(Msg& out) {
        auto& s = state();
        if (!s.hasLatest.load(std::memory_order_acquire)) return false;
        portENTER_CRITICAL(&s.mux);
        out = s.latest;
        portEXIT_CRITICAL(&s.mux);
        return true;
    }

    static bool take(Msg& out) {
        auto& s = state();
        if (!s.hasLatest.load(std::memory_order_acquire)) return false;
        portENTER_CRITICAL(&s.mux);
        out = s.latest;
        s.hasLatest.store(false, std::memory_order_release);
        portEXIT_CRITICAL(&s.mux);
        return true;
    }

    static bool hasSubscribers() { return state().subCount.load(std::memory_order_acquire) > 0; }
};
