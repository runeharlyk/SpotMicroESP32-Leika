#pragma once

#include <FS.h>
#include <template/stateful_service.h>
#include <filesystem.h>
#include <pb_encode.h>
#include <pb_decode.h>

#define PROTO_FILE_BUFFER_SIZE 1024

template <class T, class ProtoT>
class FSPersistence {
  public:
    FSPersistence(ProtoStateReader<T, ProtoT> stateReader, ProtoStateUpdater<T, ProtoT> stateUpdater,
                  StatefulService<T>* statefulService, const char* filePath, const pb_msgdesc_t* fields)
        : _stateReader(stateReader),
          _stateUpdater(stateUpdater),
          _statefulService(statefulService),
          _filePath(filePath),
          _fields(fields),
          _updateHandlerId(0) {
        enableUpdateHandler();
    }

    void readFromFS() {
        File settingsFile = _fs->open(_filePath, "r");

        if (settingsFile) {
            size_t fileSize = settingsFile.size();
            if (fileSize > 0 && fileSize <= PROTO_FILE_BUFFER_SIZE) {
                uint8_t buffer[PROTO_FILE_BUFFER_SIZE];
                size_t bytesRead = settingsFile.read(buffer, fileSize);
                settingsFile.close();

                if (bytesRead == fileSize) {
                    ProtoT proto = {};
                    pb_istream_t stream = pb_istream_from_buffer(buffer, fileSize);
                    if (pb_decode(&stream, _fields, &proto)) {
                        _statefulService->updateWithoutPropagation(proto, _stateUpdater);
                        return;
                    }
                }
            }
            settingsFile.close();
        }

        applyDefaults();
        writeToFS();
    }

    bool writeToFS() {
        ProtoT proto = {};
        _statefulService->read(proto, _stateReader);

        uint8_t buffer[PROTO_FILE_BUFFER_SIZE];
        pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

        if (!pb_encode(&stream, _fields, &proto)) {
            ESP_LOGE("FSPersistence", "Failed to encode proto");
            return false;
        }

        mkdirs();

        File file = _fs->open(_filePath, "w");
        if (!file) return false;

        size_t written = file.write(buffer, stream.bytes_written);
        file.close();
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
            _updateHandlerId = _statefulService->addUpdateHandler([&](const std::string& originId) { writeToFS(); });
        }
    }

  private:
    ProtoStateReader<T, ProtoT> _stateReader;
    ProtoStateUpdater<T, ProtoT> _stateUpdater;
    StatefulService<T>* _statefulService;
    FS* _fs {&ESP_FS};
    const char* _filePath;
    const pb_msgdesc_t* _fields;
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
    virtual void applyDefaults() {
        ProtoT proto = {};
        _statefulService->updateWithoutPropagation(proto, _stateUpdater);
    }
};
