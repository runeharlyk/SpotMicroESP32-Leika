#pragma once

#include <list>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string>

#include <template/state_result.h>

using HandlerId = size_t;
using StateUpdateCallback = std::function<void(const std::string &originId)>;

class HandlerBase {
  protected:
    static inline HandlerId nextId_ = 1;
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

    void invoke(const std::string &originId) const { callback_(originId); }
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

    StateUpdateResult update(std::function<StateUpdateResult(T &)> stateUpdater, const std::string &originId) {
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

    void read(std::function<void(T &)> stateReader) {
        lock();
        stateReader(state_);
        unlock();
    }

    void read(std::function<void(const T &)> stateReader) const {
        const_cast<StatefulService *>(this)->lock();
        stateReader(state_);
        const_cast<StatefulService *>(this)->unlock();
    }

    void callUpdateHandlers(const std::string &originId) {
        for (const UpdateHandler &updateHandler : updateHandlers_) {
            updateHandler.invoke(originId);
        }
    }

    T &state() { return state_; }

  private:
    T state_;

    inline void lock() { xSemaphoreTakeRecursive(mutex_, portMAX_DELAY); }
    inline void unlock() { xSemaphoreGiveRecursive(mutex_); }

    void notifyStateChange(const std::string &originId, StateUpdateResult &result) {
        if (result == StateUpdateResult::CHANGED) {
            callUpdateHandlers(originId);
        }
    }

    SemaphoreHandle_t mutex_;
    std::list<UpdateHandler> updateHandlers_;
};
