#pragma once
#include <array>
#include <bitset>
#include <functional>
#include <optional>
#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

enum class EmitMode { Latest, Batch };

template <typename Msg, size_t QueueDepth = 64, size_t MaxSubs = 8, size_t BatchSize = 16>
class EventBus {
    static_assert(BatchSize > 0);

    struct Item {
        Msg payload;
        uint8_t exclude; // 0-MaxSubs-1 or 0xFF for “none”
    };

    using ExIdx = uint8_t;

    static constexpr ExIdx NO_EX = 0xFF;

    struct Sub {
        std::function<void(const Msg*, size_t)> cb;
        TickType_t interval;
        TickType_t last;
        EmitMode mode;
        std::array<Msg, BatchSize> buf;
        size_t cnt;
    };

    inline static StaticQueue_t q_buf {};
    inline static Item q_storage[QueueDepth];
    inline static QueueHandle_t q_handle =
        xQueueCreateStatic(QueueDepth, sizeof(Item), reinterpret_cast<uint8_t*>(q_storage), &q_buf);

    inline static std::array<std::optional<Sub>, MaxSubs> subs {};
    inline static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    inline static Msg latest {};
    inline static volatile bool has_latest = false;
    inline static std::atomic<size_t> sub_cnt {0};

    static void store(const Msg& m) {
        portENTER_CRITICAL(&mux);
        latest = m;
        has_latest = true;
        portEXIT_CRITICAL(&mux);
    }

    static void storeISR(const Msg& m) {
        UBaseType_t s = portSET_INTERRUPT_MASK_FROM_ISR();
        latest = m;
        has_latest = true;
        portCLEAR_INTERRUPT_MASK_FROM_ISR(s);
    }

    static void dispatch(const Msg& m, Sub* ex) {
        store(m);
        TickType_t now = xTaskGetTickCount();
        for (auto& sref : subs) {
            if (!sref) continue;
            auto& sub = *sref;
            if (&sub == ex) continue;
            TickType_t dt = now - sub.last;
            if (sub.interval && dt < sub.interval) {
                if (sub.mode == EmitMode::Batch && sub.cnt < BatchSize)
                    sub.buf[sub.cnt++] = m;
                else if (sub.mode == EmitMode::Latest)
                    sub.buf[0] = m, sub.cnt = 1;
            } else {
                if (sub.cnt == 0)
                    sub.cb(&m, 1);
                else {
                    sub.cb(sub.buf.data(), sub.cnt);
                    sub.cnt = 0;
                }
                sub.last = now;
            }
        }
    }

    static void worker(void*) {
        Item it;
        for (;;)
            if (xQueueReceive(q_handle, &it, portMAX_DELAY) == pdTRUE)
                dispatch(it.payload, it.exclude == NO_EX ? nullptr : &*subs[it.exclude]);
    }

    static void ensureTask() {
        static bool once =
            (xTaskCreatePinnedToCore(worker, "eventbus", 4096, nullptr, 6, nullptr, tskNO_AFFINITY), true);
        (void)once;
    }

    static void push(const Msg& m, uint8_t ex = NO_EX) {
        Item it {m, ex};
        xQueueSend(q_handle, &it, portMAX_DELAY);
    }

  public:
    class Handle {
        size_t index {MaxSubs};
        friend class EventBus;
        explicit Handle(size_t i) : index(i) {}

      public:
        Handle() = default;
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        Handle(Handle&& h) noexcept : index(h.index) { h.index = MaxSubs; }
        Handle& operator=(Handle&& h) noexcept {
            if (this != &h) {
                unsubscribe();
                index = h.index;
                h.index = MaxSubs;
            }
            return *this;
        }
        ~Handle() = default;
        void unsubscribe() {
            if (index < MaxSubs) {
                subs[index].reset();
                sub_cnt.fetch_sub(1, std::memory_order_acq_rel);
                index = MaxSubs;
            }
        }
        bool valid() const { return index < MaxSubs; }
    };

    template <typename C>
    static Handle subscribe(uint32_t ms, EmitMode mode, C&& fn) {
        ensureTask();
        for (size_t i = 0; i < MaxSubs; ++i)
            if (!subs[i]) {
                subs[i].emplace(Sub {.cb = std::forward<C>(fn),
                                     .interval = pdMS_TO_TICKS(ms),
                                     .last = xTaskGetTickCount(),
                                     .mode = mode,
                                     .cnt = 0});
                sub_cnt.fetch_add(1, std::memory_order_acq_rel);
                return Handle(i);
            }
        return Handle(MaxSubs);
    }

    template <typename C>
    static Handle subscribe(C&& fn) {
        using F = std::decay_t<C>;
        if constexpr (std::is_invocable_v<F, const Msg&>)
            return subscribe(0, EmitMode::Latest, [f = std::forward<C>(fn)](const Msg* p, size_t n) {
                for (size_t i = 0; i < n; ++i) f(p[i]);
            });
        else
            return subscribe(0, EmitMode::Latest, std::forward<C>(fn));
    }

    template <typename C>
    static Handle subscribe(uint32_t ms, C&& fn) {
        using F = std::decay_t<C>;
        if constexpr (std::is_invocable_v<F, const Msg&>)
            return subscribe(ms, EmitMode::Batch, [f = std::forward<C>(fn)](const Msg* p, size_t n) {
                for (size_t i = 0; i < n; ++i) f(p[i]);
            });
        else
            return subscribe(ms, EmitMode::Batch, std::forward<C>(fn));
    }

    static void publish(const Msg& m) {
        store(m);
        push(m);
    }

    static void publishAsync(const Msg& m, const Handle& ex) {
        store(m);
        push(m, ex.valid() ? ex.index : NO_EX);
    }

    static void publish(const Msg& m, const Handle& ex) {
        if (ex.valid())
            dispatch(m, &*subs[ex.index]);
        else
            publish(m);
    }

    static void publishISR(const Msg& m, BaseType_t* hpw = nullptr) {
        storeISR(m);
        Item it {m, NO_EX};
        xQueueSendFromISR(q_handle, &it, hpw);
    }

    static bool peek(Msg& out) {
        if (!has_latest) return false;
        portENTER_CRITICAL(&mux);
        out = latest;
        portEXIT_CRITICAL(&mux);
        return true;
    }

    static bool take(Msg& out) {
        if (!has_latest) return false;
        portENTER_CRITICAL(&mux);
        out = latest;
        has_latest = false;
        portEXIT_CRITICAL(&mux);
        return true;
    }

    static bool hasSubscribers() { return sub_cnt.load(std::memory_order_acquire) != 0; }
};