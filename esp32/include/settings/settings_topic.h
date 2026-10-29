#pragma once

// A settings group as a state topic (communication/topics.h). Its snapshot, which a subscribe and every push send,
// reads the group through the service's proto handler, as a reply does, so a stored password goes out masked
// (settings/app_network_settings.h). The copy lives in a buffer of the topic's own, allocated on first use and kept,
// rather than on the stack of the link task that takes the snapshot: the peripheral settings are over 2 KB.
//
// Locks, in the order taken: the link's encode lock (CommAdapterBase::subscribe, push and resendDirty), the buffer's,
// then the service's. The push after a change runs from an update handler, which StatefulService::update calls once it
// has unlocked, so no path holds the service's lock while it waits for an encode lock.

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <communication/comm_base.hpp>
#include <template/stateful_proto_handler.h>

template <class T>
class SettingsTopic {
  public:
    explicit SettingsTopic(StatefulProtoHandler<T, T>& handler) : handler_(handler), mutex_(xSemaphoreCreateMutex()) {}
    SettingsTopic(const SettingsTopic&) = delete;
    SettingsTopic& operator=(const SettingsTopic&) = delete;

    // Without memory for the copy it puts nothing, so a subscribe is left unacked and a push marked for a resend.
    void snapshot(SnapshotSink& sink) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (!copy_)
            copy_ = static_cast<T*>(heap_caps_malloc_prefer(sizeof(T), 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_INTERNAL));
        if (copy_) {
            handler_.read(*copy_);
            sink.put(*copy_);
        } else {
            ESP_LOGE("SettingsTopic", "No memory for a %u byte settings copy", (unsigned)sizeof(T));
        }
        xSemaphoreGive(mutex_);
    }

  private:
    StatefulProtoHandler<T, T>& handler_;
    SemaphoreHandle_t mutex_;
    T* copy_ = nullptr;
};
