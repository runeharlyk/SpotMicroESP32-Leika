#pragma once

#include <template/stateful_service.h>
#include <template/state_result.h>
#include <filesystem.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <cstdio>
#include <sys/stat.h>
#include <esp_log.h>
#include <vector>

static const char *TAG_PERSISTENCE = "FSPersistencePB";

template <class T>
class FSPersistencePB {
  public:
    using ProtoStateReader = std::function<void(const T &, T &)>;
    using ProtoStateUpdater = std::function<StateUpdateResult(const T &, T &)>;

    FSPersistencePB(ProtoStateReader stateReader, ProtoStateUpdater stateUpdater, StatefulService<T> *statefulService,
                    const char *filePath, const pb_msgdesc_t *msgDescriptor, size_t maxSize, const T &defaultState)
        : _stateReader(stateReader),
          _stateUpdater(stateUpdater),
          _statefulService(statefulService),
          _filePath(filePath),
          _msgDescriptor(msgDescriptor),
          _maxSize(maxSize),
          _defaultState(defaultState),
          _updateHandlerId(0) {
        enableUpdateHandler();
    }

    void readFromFS() {
        if (loadFromFS()) return;
        keepUnloadableFile();
        applyDefaults();
        writeToFS();
    }

    bool writeToFS() {
        std::vector<uint8_t> buffer(_maxSize);
        pb_ostream_t stream = pb_ostream_from_buffer(buffer.data(), _maxSize);

        T protoMsg = {};
        _statefulService->read([this, &protoMsg](const T &state) { _stateReader(state, protoMsg); });

        if (!pb_encode(&stream, _msgDescriptor, &protoMsg)) {
            ESP_LOGE(TAG_PERSISTENCE, "Failed to encode %s: %s", _filePath, PB_GET_ERROR(&stream));
            return false;
        }

        mkdirs();

        FILE *file = fopen(_filePath, "wb");
        if (!file) {
            ESP_LOGE(TAG_PERSISTENCE, "Failed to open file for writing: %s", _filePath);
            return false;
        }

        size_t written = fwrite(buffer.data(), 1, stream.bytes_written, file);
        bool closed = fclose(file) == 0;

        if (written != stream.bytes_written || !closed) {
            ESP_LOGE(TAG_PERSISTENCE, "Failed to write %s", _filePath);
            return false;
        }
        return true;
    }

    void disableUpdateHandler() {
        if (_updateHandlerId) {
            _statefulService->removeUpdateHandler(_updateHandlerId);
            _updateHandlerId = 0;
        }
    }

    void enableUpdateHandler() {
        if (!_updateHandlerId) {
            _updateHandlerId = _statefulService->addUpdateHandler([&](const std::string &originId) { writeToFS(); });
        }
    }

  private:
    ProtoStateReader _stateReader;
    ProtoStateUpdater _stateUpdater;
    StatefulService<T> *_statefulService;
    const char *_filePath;
    const pb_msgdesc_t *_msgDescriptor;
    size_t _maxSize;
    T _defaultState;
    HandlerId _updateHandlerId;

    bool loadFromFS() {
        FILE *file = fopen(_filePath, "rb");
        if (!file) return false;
        std::vector<uint8_t> buffer(_maxSize + 1);
        size_t size = fread(buffer.data(), 1, buffer.size(), file);
        fclose(file);
        if (size == 0 || size > _maxSize) return false;

        T protoMsg = {};
        pb_istream_t stream = pb_istream_from_buffer(buffer.data(), size);
        if (!pb_decode(&stream, _msgDescriptor, &protoMsg)) {
            ESP_LOGE(TAG_PERSISTENCE, "Failed to decode %s: %s", _filePath, PB_GET_ERROR(&stream));
            return false;
        }
        return _statefulService->updateWithoutPropagation([this, &protoMsg](T &state) {
            return _stateUpdater(protoMsg, state);
        }) != StateUpdateResult::ERROR;
    }

    // A file that is there but does not load was written by a firmware that stored the settings
    // differently, and may hold what took effort to enter, such as the saved WiFi networks: it is kept
    // beside the defaults that replace it rather than overwritten.
    void keepUnloadableFile() {
        struct stat st;
        if (stat(_filePath, &st) != 0 || st.st_size == 0) return;
        std::string backup = std::string(_filePath) + ".bak";
        remove(backup.c_str());
        if (rename(_filePath, backup.c_str()) == 0) {
            ESP_LOGW(TAG_PERSISTENCE, "Kept unloadable %s as %s", _filePath, backup.c_str());
        } else {
            ESP_LOGE(TAG_PERSISTENCE, "Failed to keep unloadable %s", _filePath);
        }
    }

    void mkdirs() {
        std::string path(_filePath);
        size_t index = 0;
        while ((index = path.find('/', index + 1)) != std::string::npos) {
            std::string segment = path.substr(0, index);
            struct stat st;
            if (stat(segment.c_str(), &st) != 0) {
                FileSystem::mkdirRecursive(segment.c_str());
            }
        }
    }

  protected:
    void applyDefaults() {
        _statefulService->updateWithoutPropagation([this](T &state) { return _stateUpdater(_defaultState, state); });
    }
};
