#pragma once
#include <event_bus/typed_event_bus.h>
#include <event_bus/event_registry.h>
#include <event_bus/event_metadata.h>
#include <esp_timer.h>
#include <atomic>

class EventBus {
  public:
    template <typename Msg>
    using Bus = TypedEventBus<Msg, EventBusConfig<Msg>::QueueDepth, EventBusConfig<Msg>::MaxSubs,
                              EventBusConfig<Msg>::BatchSize>;

    template <typename Msg>
    using Handle = typename Bus<Msg>::Handle;

    template <typename Msg>
    static bool publish(const Msg& msg, const char* source = nullptr) {
        if (_hasGlobalListeners.load(std::memory_order_acquire)) {
            notifyGlobalListeners(msg, source);
        }

        return Bus<Msg>::publish(msg);
    }

    template <typename Msg, typename Callback>
    static auto subscribe(Callback&& callback) {
        return Bus<Msg>::subscribe(std::forward<Callback>(callback));
    }

    template <typename Msg, typename Callback>
    static auto subscribe(uint32_t intervalMs, Callback&& callback) {
        return Bus<Msg>::subscribe(intervalMs, std::forward<Callback>(callback));
    }

    template <typename Msg>
    static void publishISR(const Msg& msg, BaseType_t* higherPriorityTaskWoken = nullptr) {
        Bus<Msg>::publishISR(msg, higherPriorityTaskWoken);
    }

    template <typename Msg>
    static bool peek(Msg& out) {
        return Bus<Msg>::peek(out);
    }

    template <typename Msg>
    static bool take(Msg& out) {
        return Bus<Msg>::take(out);
    }

    template <typename Msg>
    static bool hasSubscribers() {
        return Bus<Msg>::hasSubscribers();
    }

    using GlobalHandler = FixedFn<void(EventType, const void*, size_t, uint64_t), 64>;
    static size_t subscribeGlobal(GlobalHandler&& handler);
    static void unsubscribeGlobal(size_t id);

  private:
    static std::atomic<bool> _hasGlobalListeners;

    template <typename Msg>
    static void notifyGlobalListeners(const Msg& msg, const char* source);
};
