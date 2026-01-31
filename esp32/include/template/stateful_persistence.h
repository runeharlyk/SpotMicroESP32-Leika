#ifndef FSPersistence_h
#define FSPersistence_h

#include <template/stateful_service.h>
#include <filesystem.h>
#include <ArduinoJson.h>
#include <cstdio>
#include <sys/stat.h>

template <class T>
class FSPersistence {
  public:
    FSPersistence(JsonStateReader<T> stateReader, JsonStateUpdater<T> stateUpdater, StatefulService<T> *statefulService,
                  const char *filePath)
        : _stateReader(stateReader),
          _stateUpdater(stateUpdater),
          _statefulService(statefulService),
          _filePath(filePath),
          _updateHandlerId(0) {
        enableUpdateHandler();
    }

    void readFromFS() {
        std::string content = FileSystem::readFile(_filePath);

        if (!content.empty()) {
            JsonDocument jsonDocument;
            DeserializationError error = deserializeJson(jsonDocument, content);
            if (error == DeserializationError::Ok) {
                JsonVariant jsonObject = jsonDocument.as<JsonVariant>();
                _statefulService->updateWithoutPropagation(jsonObject, _stateUpdater);
                return;
            }
        }

        applyDefaults();
        writeToFS();
    }

    bool writeToFS() {
        JsonDocument jsonDocument;
        JsonVariant jsonObject = jsonDocument.to<JsonVariant>();
        _statefulService->read(jsonObject, _stateReader);

        mkdirs();

        std::string content;
        serializeJson(jsonDocument, content);

        return FileSystem::writeFile(_filePath, content.c_str());
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
    JsonStateReader<T> _stateReader;
    JsonStateUpdater<T> _stateUpdater;
    StatefulService<T> *_statefulService;
    const char *_filePath;
    size_t _bufferSize;
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
    virtual void applyDefaults() {
        JsonDocument jsonDocument;
        JsonVariant jsonObject = jsonDocument.as<JsonVariant>();
        _statefulService->updateWithoutPropagation(jsonObject, _stateUpdater);
    }
};

#endif
