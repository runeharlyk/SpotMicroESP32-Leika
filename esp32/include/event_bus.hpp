#pragma once
#include <array>
#include <optional>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

template <typename Sig, size_t MaxSize>
class FixedFn;

template <typename R, typename... A, size_t MaxSize>
class FixedFn<R(A...), MaxSize> {
    alignas(void*) std::byte buf[MaxSize];
    void (*call)(void*, A&&...) {};
    void (*moveFn)(void*, void*) {};
    void (*destroy)(void*) {};

  public:
    template <typename Fun>
    void set(Fun&& f) {
        static_assert(sizeof(Fun) <= MaxSize);
        new (buf) Fun(std::forward<Fun>(f));
        call = [](void* p, A&&... as) { (*reinterpret_cast<Fun*>(p))(std::forward<A>(as)...); };
        moveFn = [](void* d, void* s) { new (d) Fun(std::move(*reinterpret_cast<Fun*>(s))); };
        destroy = [](void* p) { reinterpret_cast<Fun*>(p)->~Fun(); };
    }
    R operator()(A... as) const {
        return call(const_cast<void*>(static_cast<const void*>(buf)), std::forward<A>(as)...);
    }
    FixedFn() = default;
    FixedFn(FixedFn&& o) {
        if (o.moveFn) o.moveFn(buf, o.buf);
        call = o.call;
        moveFn = o.moveFn;
        destroy = o.destroy;
        o.destroy = nullptr;
    }
    FixedFn(const FixedFn& o) {
        std::memcpy(buf, o.buf, MaxSize);
        call = o.call;
        moveFn = o.moveFn;
        destroy = o.destroy;
    }
    ~FixedFn() {
        if (destroy) destroy(buf);
    }
    FixedFn& operator=(const FixedFn&) = delete;
    FixedFn& operator=(FixedFn&&) = delete;
};

enum class EmitMode { Latest, Batch };

template <typename Msg, size_t QueueDepth = 64, size_t MaxSubs = 8, size_t BatchSize = 16>
class EventBus {
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

    static void store(const Msg& m) {
        portENTER_CRITICAL(&mux);
        latest = m;
        hasLatest.store(true, std::memory_order_release);
        portEXIT_CRITICAL(&mux);
    }
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
            TickType_t dt = now - s.last;

            if (s.interval && dt < s.interval) {
                if (s.mode == EmitMode::Batch && s.cnt < BatchSize)
                    s.buf[s.cnt++] = m;
                else if (s.mode == EmitMode::Latest) {
                    s.buf[0] = m;
                    s.cnt = 1;
                }
            } else {
                s.buf[s.cnt++] = m;
                s.last = now;
                ready[readyCnt++] = &s;
            }
        }
        portEXIT_CRITICAL(&mux);

        for (size_t i = 0; i < readyCnt; ++i) {
            Sub* s = ready[i];
            s->cb(s->buf.data(), s->cnt);
            s->cnt = 0;
        }
    }

    static void worker(void*) {
        Item it;
        while (xQueueReceive(queue, &it, portMAX_DELAY) == pdTRUE) dispatch(it.payload, it.exclude);
    }
    static void ensureTask() {
        static bool once = (xTaskCreatePinnedToCore(worker, "evtbus", 4096, nullptr, 6, nullptr, 1), true);
        (void)once;
    }
    static bool push(const Msg& m, size_t ex = NO_EX, TickType_t to = 0) {
        ensureTask();
        Item it {m, ex};
        return xQueueSend(queue, &it, to) == pdTRUE;
    }

  public:
    class Handle {
        size_t idx {NO_EX};
        friend class EventBus;
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
                portENTER_CRITICAL(&mux);
                subs[idx].reset();
                portEXIT_CRITICAL(&mux);
                subCount.fetch_sub(1, std::memory_order_acq_rel);
                idx = NO_EX;
            }
        }
        bool valid() const { return idx < MaxSubs; }
    };

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
                s.last = xTaskGetTickCount();
                s.mode = mode;
                s.cnt = 0;
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

    static void publish(const Msg& m) {
        store(m);
        push(m, NO_EX, portMAX_DELAY);
    }
    static void publish(const Msg& m, const Handle& h) {
        if (h.valid())
            dispatch(m, h.idx);
        else
            publish(m);
    }

    static bool publishAsync(const Msg& m) {
        store(m);
        return push(m);
    }
    static bool publishAsync(const Msg& m, const Handle& h) {
        if (h.valid()) dispatch(m, h.idx);
        return publishAsync(m);
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