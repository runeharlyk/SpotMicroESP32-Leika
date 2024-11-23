#ifndef StatefulService_h
#define StatefulService_h

/**
 *   ESP32 SvelteKit
 *
 *   A simple, secure and extensible framework for IoT projects for ESP32 platforms
 *   with responsive Sveltekit front-end built with TailwindCSS and DaisyUI.
 *   https://github.com/theelims/ESP32-sveltekit
 *
 *   Copyright (C) 2018 - 2023 rjwats
 *   Copyright (C) 2023 theelims
 *   Copyright (C) 2024 runeharlyk
 *
 *   All Rights Reserved. This software may be modified and distributed under
 *   the terms of the LGPL v3 license. See the LICENSE file for details.
 **/

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

typedef size_t update_handler_id_t;
typedef size_t hook_handler_id_t;
typedef std::function<void(const String &originId)> StateUpdateCallback;
typedef std::function<void(const String &originId, StateUpdateResult &result)> StateHookCallback;

typedef struct StateUpdateHandlerInfo {
    static inline update_handler_id_t currentUpdatedHandlerId = 0;
    update_handler_id_t _id;
    StateUpdateCallback _callback;
    bool _allowRemove;
    StateUpdateHandlerInfo(StateUpdateCallback callback, bool allowRemove)
        : _id(++currentUpdatedHandlerId), _callback(callback), _allowRemove(allowRemove) {};
} StateUpdateHandlerInfo_t;

typedef struct StateHookHandlerInfo {
    static inline hook_handler_id_t currentHookHandlerId = 0;
    hook_handler_id_t _id;
    StateHookCallback _callback;
    bool _allowRemove;
    StateHookHandlerInfo(StateHookCallback callback, bool allowRemove)
        : _id(++currentHookHandlerId), _callback(callback), _allowRemove(allowRemove) {};
} StateHookHandlerInfo_t;

template <class T>
class StatefulService {
  public:
    template <typename... Args>
    StatefulService(Args &&...args) : state_(std::forward<Args>(args)...), mutex_(xSemaphoreCreateRecursiveMutex()) {}

    update_handler_id_t addUpdateHandler(StateUpdateCallback callback, bool allowRemove = true) {
        if (!callback) return 0;

        StateUpdateHandlerInfo_t updateHandler(callback, allowRemove);
        updateHandlers_.push_back(updateHandler);
        return updateHandler._id;
    }

    void removeUpdateHandler(update_handler_id_t id) {
        for (auto i = updateHandlers_.begin(); i != updateHandlers_.end();) {
            if ((*i)._allowRemove && (*i)._id == id) {
                i = updateHandlers_.erase(i);
            } else {
                ++i;
            }
        }
    }

    hook_handler_id_t addHookHandler(StateHookCallback callback, bool allowRemove = true) {
        if (!callback) return 0;

        StateHookHandlerInfo_t hookHandler(callback, allowRemove);
        hookHandlers_.push_back(hookHandler);
        return hookHandler._id;
    }

    void removeHookHandler(hook_handler_id_t id) {
        for (auto i = hookHandlers_.begin(); i != hookHandlers_.end();) {
            if ((*i)._allowRemove && (*i)._id == id) {
                i = hookHandlers_.erase(i);
            } else {
                ++i;
            }
        }
    }

    StateUpdateResult update(std::function<StateUpdateResult(T &)> stateUpdater, const String &originId) {
        beginTransaction();
        StateUpdateResult result = stateUpdater(state_);
        endTransaction();
        notifyStateChange(originId, result);
        return result;
    }

    StateUpdateResult updateWithoutPropagation(std::function<StateUpdateResult(T &)> stateUpdater) {
        beginTransaction();
        StateUpdateResult result = stateUpdater(state_);
        endTransaction();
        return result;
    }

    StateUpdateResult update(JsonObject &jsonObject, JsonStateUpdater<T> stateUpdater, const String &originId) {
        beginTransaction();
        StateUpdateResult result = stateUpdater(jsonObject, state_);
        endTransaction();
        notifyStateChange(originId, result);
        return result;
    }

    StateUpdateResult updateWithoutPropagation(JsonObject &jsonObject, JsonStateUpdater<T> stateUpdater) {
        beginTransaction();
        StateUpdateResult result = stateUpdater(jsonObject, state_);
        endTransaction();
        return result;
    }

    void read(std::function<void(T &)> stateReader) {
        beginTransaction();
        stateReader(state_);
        endTransaction();
    }

    void read(JsonObject &jsonObject, JsonStateReader<T> stateReader) {
        beginTransaction();
        stateReader(state_, jsonObject);
        endTransaction();
    }

    void callUpdateHandlers(const String &originId) {
        for (const StateUpdateHandlerInfo_t &updateHandler : updateHandlers_) {
            updateHandler._callback(originId);
        }
    }

    void callHookHandlers(const String &originId, StateUpdateResult &result) {
        for (const StateHookHandlerInfo_t &hookHandler : hookHandlers_) {
            hookHandler._callback(originId, result);
        }
    }

    T &state() { return state_; }

  private:
    T state_;

    inline void beginTransaction() { xSemaphoreTakeRecursive(mutex_, portMAX_DELAY); }
    inline void endTransaction() { xSemaphoreGiveRecursive(mutex_); }

    void notifyStateChange(const String &originId, StateUpdateResult &result) {
        callHookHandlers(originId, result);
        if (result == StateUpdateResult::CHANGED) {
            callUpdateHandlers(originId);
        }
    }

    SemaphoreHandle_t mutex_;
    std::list<StateUpdateHandlerInfo_t> updateHandlers_;
    std::list<StateHookHandlerInfo_t> hookHandlers_;
};

#endif // end StatefulService_h
