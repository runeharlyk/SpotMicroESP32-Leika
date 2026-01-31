#pragma once

#include <template/stateful_service.h>
#include <template/state_result.h>
#include <filesystem.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <cstdio>
#include <sys/stat.h>
#include <esp_log.h>

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
        FILE *file = fopen(_filePath, "rb");

        if (file) {
            fseek(file, 0, SEEK_END);
            size_t fileSize = ftell(file);
            fseek(file, 0, SEEK_SET);

            if (fileSize > 0 && fileSize <= _maxSize) {
                uint8_t *buffer = new uint8_t[fileSize];
                size_t bytesRead = fread(buffer, 1, fileSize, file);
                fclose(file);

                if (bytesRead == fileSize) {
                    T *protoMsg = new T();
                    *protoMsg = {};
                    pb_istream_t stream = pb_istream_from_buffer(buffer, bytesRead);

                    if (pb_decode(&stream, _msgDescriptor, protoMsg)) {
                        _statefulService->updateWithoutPropagation(
                            [this, protoMsg](T &state) { return _stateUpdater(*protoMsg, state); });
                        delete protoMsg;
                        delete[] buffer;
                        return;
                    }
                    delete protoMsg;
                }
                delete[] buffer;
            } else {
                fclose(file);
            }
        }

        applyDefaults();
        writeToFS();
    }

    bool writeToFS() {
        uint8_t *buffer = new uint8_t[_maxSize];
        pb_ostream_t stream = pb_ostream_from_buffer(buffer, _maxSize);

        T *protoMsg = new T();
        *protoMsg = {};
        _statefulService->read([this, protoMsg](const T &state) { _stateReader(state, *protoMsg); });

        bool encodeSuccess = pb_encode(&stream, _msgDescriptor, protoMsg);
        delete protoMsg;

        if (!encodeSuccess) {
            delete[] buffer;
            return false;
        }

        mkdirs();

        FILE *file = fopen(_filePath, "wb");
        if (!file) {
            ESP_LOGE(TAG_PERSISTENCE, "Failed to open file for writing: %s", _filePath);
            delete[] buffer;
            return false;
        }

        size_t written = fwrite(buffer, 1, stream.bytes_written, file);
        fclose(file);
        delete[] buffer;

        return written == stream.bytes_written;
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
