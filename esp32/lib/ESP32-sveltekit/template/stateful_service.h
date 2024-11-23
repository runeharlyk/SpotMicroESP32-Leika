#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <list>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <template/state_result.h>

template <typename T>
using JsonStateUpdater = std::function<StateUpdateResult(JsonObject &root, T &settings)>;

template <typename T>
using JsonStateReader = std::function<void(T &settings, JsonObject &root)>;

using HandlerId = size_t;
using StateUpdateCallback = std::function<void(const String &originId)>;
using StateHookCallback = std::function<void(const String &originId, StateUpdateResult &result)>;

class HandlerBase {
  protected:
    static inline HandlerId nextId_ = 1; // Start from 1, 0 is invalid
    HandlerId id_;
    bool allowRemove_;

    HandlerBase(bool allowRemove) : id_(nextId_++), allowRemove_(allowRemove) {}

  public:
    HandlerId getId() const { return id_; }
    bool isRemovable() const { return allowRemove_; }
};

class UpdateHandler : public HandlerBase {
    StateUpdateCallback callback_;

  public:
    UpdateHandler(StateUpdateCallback callback, bool allowRemove)
        : HandlerBase(allowRemove), callback_(std::move(callback)) {}

    void invoke(const String &originId) const { callback_(originId); }
};

class HookHandler : public HandlerBase {
    StateHookCallback callback_;

  public:
    HookHandler(StateHookCallback callback, bool allowRemove)
        : HandlerBase(allowRemove), callback_(std::move(callback)) {}

    void invoke(const String &originId, StateUpdateResult &result) const { callback_(originId, result); }
};

template <class T>
class StatefulService {
  public:
    template <typename... Args>
    StatefulService(Args &&...args) : state_(std::forward<Args>(args)...), mutex_(xSemaphoreCreateRecursiveMutex()) {}

    HandlerId addUpdateHandler(StateUpdateCallback callback, bool allowRemove = true) {
        if (!callback) return 0;

        updateHandlers_.emplace_back(std::move(callback), allowRemove);
        return updateHandlers_.back().getId();
    }

    void removeUpdateHandler(HandlerId id) {
        updateHandlers_.remove_if(
            [id](const UpdateHandler &handler) { return handler.isRemovable() && handler.getId() == id; });
    }

    HandlerId addHookHandler(StateHookCallback callback, bool allowRemove = true) {
        if (!callback) return 0;

        hookHandlers_.emplace_back(std::move(callback), allowRemove);
        return hookHandlers_.back().getId();
    }

    void removeHookHandler(HandlerId id) {
        hookHandlers_.remove_if(
            [id](const HookHandler &handler) { return handler.isRemovable() && handler.getId() == id; });
    }

    StateUpdateResult update(std::function<StateUpdateResult(T &)> stateUpdater, const String &originId) {
        lock();
        StateUpdateResult result = stateUpdater(state_);
        unlock();
        notifyStateChange(originId, result);
        return result;
    }

    StateUpdateResult updateWithoutPropagation(std::function<StateUpdateResult(T &)> stateUpdater) {
        lock();
        StateUpdateResult result = stateUpdater(state_);
        unlock();
        return result;
    }

    StateUpdateResult update(JsonObject &jsonObject, JsonStateUpdater<T> stateUpdater, const String &originId) {
        lock();
        StateUpdateResult result = stateUpdater(jsonObject, state_);
        unlock();
        notifyStateChange(originId, result);
        return result;
    }

    StateUpdateResult updateWithoutPropagation(JsonObject &jsonObject, JsonStateUpdater<T> stateUpdater) {
        lock();
        StateUpdateResult result = stateUpdater(jsonObject, state_);
        unlock();
        return result;
    }

    void read(std::function<void(T &)> stateReader) {
        lock();
        stateReader(state_);
        unlock();
    }

    void read(JsonObject &jsonObject, JsonStateReader<T> stateReader) {
        lock();
        stateReader(state_, jsonObject);
        unlock();
    }

    void callUpdateHandlers(const String &originId) {
        for (const UpdateHandler &updateHandler : updateHandlers_) {
            updateHandler.invoke(originId);
        }
    }

    void callHookHandlers(const String &originId, StateUpdateResult &result) {
        for (const HookHandler &hookHandler : hookHandlers_) {
            hookHandler.invoke(originId, result);
        }
    }

    T &state() { return state_; }

  private:
    T state_;

    inline void lock() { xSemaphoreTakeRecursive(mutex_, portMAX_DELAY); }
    inline void unlock() { xSemaphoreGiveRecursive(mutex_); }

    void notifyStateChange(const String &originId, StateUpdateResult &result) {
        callHookHandlers(originId, result);
        if (result == StateUpdateResult::CHANGED) {
            callUpdateHandlers(originId);
        }
    }

    SemaphoreHandle_t mutex_;
    std::list<UpdateHandler> updateHandlers_;
    std::list<HookHandler> hookHandlers_;
};
