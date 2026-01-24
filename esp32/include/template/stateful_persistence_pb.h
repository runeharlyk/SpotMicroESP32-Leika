#pragma once

#include <FS.h>
#include <template/stateful_service.h>
#include <template/state_result.h>
#include <filesystem.h>
#include <pb_encode.h>
#include <pb_decode.h>

/**
 * Protobuf-based filesystem persistence for StatefulService.
 *
 * @tparam T The state type (should be a nanopb-generated struct like api_APSettings)
 */
template <class T>
class FSPersistencePB {
  public:
    // Formats are passed as referenced const (local variable) we want to read from, and a reference (proto) we write to
    using ProtoStateReader = std::function<void(const T&, T&)>;
    // Formats are passed as referenced const (new object) we read from, and a reference to the local variable we  write to
    using ProtoStateUpdater = std::function<StateUpdateResult(const T&, T&)>;

    FSPersistencePB(ProtoStateReader stateReader, ProtoStateUpdater stateUpdater,
                    StatefulService<T> *statefulService, const char *filePath,
                    const pb_msgdesc_t *msgDescriptor, size_t maxSize,
                    const T &defaultState)
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
        File file = _fs->open(_filePath, "r");

        if (file) {
            size_t fileSize = file.size();
            if (fileSize > 0 && fileSize <= _maxSize) {
                uint8_t *buffer = new uint8_t[fileSize];
                size_t bytesRead = file.read(buffer, fileSize);
                file.close();

                if (bytesRead == fileSize) {
                    T protoMsg = {};
                    pb_istream_t stream = pb_istream_from_buffer(buffer, bytesRead);

                    if (pb_decode(&stream, _msgDescriptor, &protoMsg)) {
                        _statefulService->updateWithoutPropagation(
                            [this, &protoMsg](T &state) { return _stateUpdater(protoMsg, state); });
                        delete[] buffer;
                        return;
                    }
                }
                delete[] buffer;
            } else {
                file.close();
            }
        }

        applyDefaults();
        writeToFS();
    }

    bool writeToFS() {
        uint8_t *buffer = new uint8_t[_maxSize];
        pb_ostream_t stream = pb_ostream_from_buffer(buffer, _maxSize);

        T protoMsg = {};
        _statefulService->read([this, &protoMsg](const T &state) { _stateReader(state, protoMsg); });

        bool encodeSuccess = pb_encode(&stream, _msgDescriptor, &protoMsg);

        if (!encodeSuccess) {
            delete[] buffer;
            return false;
        }

        mkdirs();

        File file = _fs->open(_filePath, "w");
        if (!file) {
            delete[] buffer;
            return false;
        }

        size_t written = file.write(buffer, stream.bytes_written);
        file.close();
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
            _updateHandlerId = _statefulService->addUpdateHandler(
                [&](const std::string &originId) { writeToFS(); });
        }
    }

  private:
    ProtoStateReader _stateReader;
    ProtoStateUpdater _stateUpdater;
    StatefulService<T> *_statefulService;
    FS *_fs{&ESP_FS};
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
            if (!_fs->exists(segment.c_str())) _fs->mkdir(segment.c_str());
        }
    }

  protected:
    void applyDefaults() {
        _statefulService->updateWithoutPropagation(
            [this](T &state) { return _stateUpdater(_defaultState, state); });
    }
};
