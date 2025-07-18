#ifndef FSPersistence_h
#define FSPersistence_h

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

#include <FS.h>
#include <template/stateful_service.h>
#include <filesystem.h>

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
        File settingsFile = _fs->open(_filePath, "r");

        if (settingsFile) {
            JsonDocument jsonDocument;
            DeserializationError error = deserializeJson(jsonDocument, settingsFile);
            if (error == DeserializationError::Ok) {
                JsonVariant jsonObject = jsonDocument.as<JsonVariant>();
                _statefulService->updateWithoutPropagation(jsonObject, _stateUpdater);
                settingsFile.close();
                return;
            }
            settingsFile.close();
        }

        // If we reach here we have not been successful in loading the config
        // and hard-coded defaults are now applied. The settings are then
        // written back to the file system so the defaults persist between
        // resets. This last step is required as in some cases defaults contain
        // randomly generated values which would otherwise be modified on reset.
        applyDefaults();
        writeToFS();
    }

    bool writeToFS() {
        JsonDocument jsonDocument;
        JsonVariant jsonObject = jsonDocument.to<JsonVariant>();
        _statefulService->read(jsonObject, _stateReader);

        mkdirs();

        File file = _fs->open(_filePath, "w");

        if (!file) return false;

        serializeJson(jsonDocument, file);
        file.close();
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
            _updateHandlerId = _statefulService->addUpdateHandler([&](const String &originId) { writeToFS(); });
        }
    }

  private:
    JsonStateReader<T> _stateReader;
    JsonStateUpdater<T> _stateUpdater;
    StatefulService<T> *_statefulService;
    FS *_fs {&ESPFS};
    const char *_filePath;
    size_t _bufferSize;
    HandlerId _updateHandlerId;

    // We assume we have a _filePath with format
    // "/directory1/directory2/filename" We create a directory for each missing
    // parent
    void mkdirs() {
        String path(_filePath);
        int index = 0;
        while ((index = path.indexOf('/', index + 1)) != -1) {
            String segment = path.substring(0, index);
            if (!_fs->exists(segment)) _fs->mkdir(segment);
        }
    }

  protected:
    // We assume the updater supplies sensible defaults if an empty object
    // is supplied, this virtual function allows that to be changed.
    virtual void applyDefaults() {
        JsonDocument jsonDocument;
        JsonVariant jsonObject = jsonDocument.as<JsonVariant>();
        _statefulService->updateWithoutPropagation(jsonObject, _stateUpdater);
    }
};

#endif // end FSPersistence
