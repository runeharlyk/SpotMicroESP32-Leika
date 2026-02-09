#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include <list>
#include <map>
#include <cstdint>

class EventBus;

class SubscriptionHandle {
  public:
    SubscriptionHandle() = default;

    ~SubscriptionHandle() { unsubscribe(); }

    SubscriptionHandle(SubscriptionHandle&& other) noexcept
        : typeId_(other.typeId_), handlerId_(other.handlerId_), bus_(other.bus_) {
        other.bus_ = nullptr;
    }

    SubscriptionHandle& operator=(SubscriptionHandle&& other) noexcept {
        if (this != &other) {
            unsubscribe();
            typeId_ = other.typeId_;
            handlerId_ = other.handlerId_;
            bus_ = other.bus_;
            other.bus_ = nullptr;
        }
        return *this;
    }

    SubscriptionHandle(const SubscriptionHandle&) = delete;
    SubscriptionHandle& operator=(const SubscriptionHandle&) = delete;

    void unsubscribe();

  private:
    friend class EventBus;
    using TypeId = const void*;

    SubscriptionHandle(TypeId typeId, uint32_t handlerId, EventBus* bus)
        : typeId_(typeId), handlerId_(handlerId), bus_(bus) {}

    TypeId typeId_ = nullptr;
    uint32_t handlerId_ = 0;
    EventBus* bus_ = nullptr;
};

class EventBus {
  public:
    using TypeId = const void*;

    template <typename T>
    static TypeId typeId() {
        static const char id = 0;
        return &id;
    }

    static EventBus& instance() {
        static EventBus bus;
        return bus;
    }

    template <typename T>
    SubscriptionHandle subscribe(std::function<void(const T&)> callback) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        auto id = typeId<T>();
        uint32_t hid = nextHandlerId_++;
        handlers_[id].push_back(
            {hid, [cb = std::move(callback)](const void* data) { cb(*static_cast<const T*>(data)); }});
        xSemaphoreGive(mutex_);
        return SubscriptionHandle(id, hid, this);
    }

    template <typename T>
    void publish(const T& event) {
        xSemaphoreTake(mutex_, portMAX_DELAY);

        auto id = typeId<T>();
        auto& entry = cache_[id];
        if (entry.data) {
            *static_cast<T*>(entry.data) = event;
        } else {
            entry.data = new T(event);
            entry.deleter = [](void* p) { delete static_cast<T*>(p); };
        }

        auto it = handlers_.find(id);
        if (it == handlers_.end()) {
            xSemaphoreGive(mutex_);
            return;
        }
        auto snapshot = it->second;
        xSemaphoreGive(mutex_);

        for (auto& handler : snapshot) {
            handler.callback(&event);
        }
    }

    template <typename T>
    bool has() {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        auto it = cache_.find(typeId<T>());
        bool result = it != cache_.end() && it->second.data != nullptr;
        xSemaphoreGive(mutex_);
        return result;
    }

    template <typename T>
    T peek() {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        auto it = cache_.find(typeId<T>());
        T result = (it != cache_.end() && it->second.data) ? *static_cast<const T*>(it->second.data) : T {};
        xSemaphoreGive(mutex_);
        return result;
    }

  private:
    friend class SubscriptionHandle;

    EventBus() { mutex_ = xSemaphoreCreateMutex(); }

    ~EventBus() {
        for (auto& [id, entry] : cache_) {
            if (entry.deleter) entry.deleter(entry.data);
        }
        vSemaphoreDelete(mutex_);
    }

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    void removeHandler(TypeId typeId, uint32_t handlerId) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        auto it = handlers_.find(typeId);
        if (it != handlers_.end()) {
            it->second.remove_if([handlerId](const Handler& h) { return h.id == handlerId; });
        }
        xSemaphoreGive(mutex_);
    }

    struct Handler {
        uint32_t id;
        std::function<void(const void*)> callback;
    };

    struct CacheEntry {
        void* data = nullptr;
        void (*deleter)(void*) = nullptr;
    };

    SemaphoreHandle_t mutex_;
    uint32_t nextHandlerId_ = 1;
    std::map<TypeId, std::list<Handler>> handlers_;
    std::map<TypeId, CacheEntry> cache_;
};

inline void SubscriptionHandle::unsubscribe() {
    if (bus_) {
        bus_->removeHandler(typeId_, handlerId_);
        bus_ = nullptr;
    }
}
