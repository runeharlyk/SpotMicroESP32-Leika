#pragma once

#include <eventbus.hpp>
#include <pb_encode.h>
#include <pb_decode.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <cstdio>
#include <memory>

template <typename ProtoMsg, ProtoMsg (*DefaultsFn)()>
class ProtoEventStorage {
  public:
    ProtoEventStorage(const char* filename, const pb_msgdesc_t* descriptor, size_t maxSize, uint32_t debounceMs = 1000)
        : _filename(filename), _descriptor(descriptor), _maxSize(maxSize), _debounceMs(debounceMs) {}

    void begin() {
        ProtoMsg loaded {};
        loadOrDefault(loaded);
        EventBus::instance().publish(loaded);

        esp_timer_create_args_t timerArgs {};
        timerArgs.callback = timerCallback;
        timerArgs.arg = this;
        timerArgs.dispatch_method = ESP_TIMER_TASK;
        timerArgs.name = _filename;
        esp_timer_create(&timerArgs, &_timer);

        _handle = EventBus::instance().subscribe<ProtoMsg>([this](const ProtoMsg& msg) {
            _pending = msg;
            esp_timer_stop(_timer);
            esp_timer_start_once(_timer, static_cast<uint64_t>(_debounceMs) * 1000);
        });
    }

  private:
    const char* _filename;
    const pb_msgdesc_t* _descriptor;
    size_t _maxSize;
    uint32_t _debounceMs;
    SubscriptionHandle _handle;
    ProtoMsg _pending {};
    esp_timer_handle_t _timer = nullptr;

    static void timerCallback(void* arg) {
        auto* self = static_cast<ProtoEventStorage*>(arg);
        self->save(self->_pending);
    }

    void loadOrDefault(ProtoMsg& outMsg) {
        FILE* file = fopen(_filename, "rb");
        if (!file) {
            ESP_LOGI("ProtoStorage", "No file %s, using defaults", _filename);
            outMsg = DefaultsFn();
            return;
        }

        fseek(file, 0, SEEK_END);
        long tellResult = ftell(file);
        fseek(file, 0, SEEK_SET);
        if (tellResult < 0) {
            ESP_LOGE("ProtoStorage", "ftell failed for %s", _filename);
            fclose(file);
            outMsg = DefaultsFn();
            return;
        }
        size_t size = static_cast<size_t>(tellResult);

        if (size == 0 || size > _maxSize) {
            ESP_LOGW("ProtoStorage", "Invalid size %zu for %s (max %zu)", size, _filename, _maxSize);
            fclose(file);
            outMsg = DefaultsFn();
            return;
        }

        auto buffer = std::make_unique<uint8_t[]>(size);
        size_t read = fread(buffer.get(), 1, size, file);
        fclose(file);
        if (read != size) {
            ESP_LOGE("ProtoStorage", "Read %zu/%zu from %s", read, size, _filename);
            outMsg = DefaultsFn();
            return;
        }

        pb_istream_t stream = pb_istream_from_buffer(buffer.get(), size);
        if (!pb_decode(&stream, _descriptor, &outMsg)) {
            ESP_LOGE("ProtoStorage", "Decode failed for %s", _filename);
            outMsg = DefaultsFn();
        }
    }

    void save(const ProtoMsg& msg) {
        auto buffer = std::make_unique<uint8_t[]>(_maxSize);
        pb_ostream_t stream = pb_ostream_from_buffer(buffer.get(), _maxSize);

        if (!pb_encode(&stream, _descriptor, &msg)) {
            ESP_LOGE("ProtoStorage", "Encode failed for %s", _filename);
            return;
        }

        FILE* file = fopen(_filename, "wb");
        if (!file) {
            ESP_LOGE("ProtoStorage", "Open write failed for %s", _filename);
            return;
        }

        size_t written = fwrite(buffer.get(), 1, stream.bytes_written, file);
        fclose(file);
        if (written != stream.bytes_written) {
            ESP_LOGE("ProtoStorage", "Write failed for %s (%zu/%zu)", _filename, written, stream.bytes_written);
        }
    }
};
