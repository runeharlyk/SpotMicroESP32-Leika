#pragma once
#include <event_bus/event_bus.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <esp_log.h>
#include <memory>
#include <cstdio>

template <typename ProtoMsg, ProtoMsg (*DefaultsFn)()>
class ProtoEventStorage {
  public:
    ProtoEventStorage(const char* filename, const pb_msgdesc_t* descriptor, size_t maxSize, uint32_t debounceMs = 1000)
        : _filename(filename), _descriptor(descriptor), _maxSize(maxSize), _debounceMs(debounceMs) {}

    void begin() {
        auto loaded = std::unique_ptr<ProtoMsg>(new ProtoMsg {});
        loadOrDefault(*loaded);
        EventBus::publish(*loaded, "EventStorage");
        ESP_LOGI(TAG, "Loaded %s", _filename);

        _handle = EventBus::subscribe<ProtoMsg>(_debounceMs, [this](const ProtoMsg& msg) { save(msg); });
    }

  private:
    static constexpr const char* TAG = "ProtoStorage";
    const char* _filename;
    const pb_msgdesc_t* _descriptor;
    size_t _maxSize;
    uint32_t _debounceMs;
    typename EventBus::Handle<ProtoMsg> _handle;

    void loadOrDefault(ProtoMsg& outMsg) {
        FILE* file = fopen(_filename, "rb");
        if (!file) {
            outMsg = DefaultsFn();
            return;
        }

        fseek(file, 0, SEEK_END);
        size_t size = ftell(file);
        fseek(file, 0, SEEK_SET);

        if (size == 0 || size > _maxSize) {
            fclose(file);
            outMsg = DefaultsFn();
            return;
        }

        auto buffer = std::make_unique<uint8_t[]>(size);
        fread(buffer.get(), 1, size, file);
        fclose(file);

        pb_istream_t stream = pb_istream_from_buffer(buffer.get(), size);
        if (!pb_decode(&stream, _descriptor, &outMsg)) {
            outMsg = DefaultsFn();
        }
    }

    void save(const ProtoMsg& msg) {
        auto buffer = std::make_unique<uint8_t[]>(_maxSize);
        pb_ostream_t stream = pb_ostream_from_buffer(buffer.get(), _maxSize);

        if (!pb_encode(&stream, _descriptor, &msg)) return;

        FILE* file = fopen(_filename, "wb");
        if (!file) return;

        fwrite(buffer.get(), 1, stream.bytes_written, file);
        fclose(file);
        ESP_LOGD(TAG, "Saved %s", _filename);
    }
};
