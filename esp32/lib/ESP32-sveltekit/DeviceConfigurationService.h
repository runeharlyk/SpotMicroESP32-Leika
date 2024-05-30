#pragma once

#include <EventEndpoint.h>
#include <FSPersistence.h>
#include <HttpEndpoint.h>
#include <SecurityManager.h>
#include <StatefulService.h>

#include <SPI.h>
#include <Wire.h>

#define DEVICE_CONFIG_FILE "/config/deviceConfig.json"
#define EVENT_CONFIGURATION_SETTINGS "ConfigurationSettings"
#define CONFIGURATION_SETTINGS_PATH "/api/configuration"

/*
 * OLED Settings
 */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_RESET -1

/*
 * I2C software connection 
 */
#define SDA 14
#define SCL 15

/*
 * Ultra sonic sensors
 */
#define USS_LEFT 12
#define USS_RIGHT 13
#define USS_MAX_DISTANCE 200

class PinConfig {
public:
    int pin;
    String mode;
    String type;
    String role;

    PinConfig(int p, String m, String t, String r) : pin(p), mode(m), type(t), role(r) {}
};

class DeviceConfiguration {
   public:
    int sda = SDA;
    int scl = SCL;
    std::vector<PinConfig> pins;

    static void read(DeviceConfiguration &settings, JsonObject &root) {
        root["sda"] = settings.sda;
        root["scl"] = settings.scl;
    }

    static StateUpdateResult update(JsonObject &root, DeviceConfiguration &settings) {
        settings.sda = root["sda"] | SDA;
        settings.scl = root["scl"] | SCL;
        return StateUpdateResult::CHANGED;
    };
};

class DeviceConfigurationService : public StatefulService<DeviceConfiguration> {
   public:
    DeviceConfigurationService(PsychicHttpServer *server, FS *fs,
                               SecurityManager *securityManager,
                               EventSocket *socket)
        : _server(server),
          _securityManager(securityManager),
          _httpEndpoint(DeviceConfiguration::read, DeviceConfiguration::update,
                        this, server, CONFIGURATION_SETTINGS_PATH,
                        securityManager, AuthenticationPredicates::IS_ADMIN),
          _eventEndpoint(DeviceConfiguration::read, DeviceConfiguration::update,
                         this, socket, EVENT_CONFIGURATION_SETTINGS),
          _fsPersistence(DeviceConfiguration::read, DeviceConfiguration::update,
                         this, fs, DEVICE_CONFIG_FILE)
                         {
                            addUpdateHandler([&](const String &originId) { updatePins(); },
                         false);
                         };

    void begin() {
        _httpEndpoint.begin();
        _eventEndpoint.begin();
        _fsPersistence.readFromFS();
        updatePins();
    };

    void updatePins() {
        if (i2c_active){
            Wire.end();
        }

        if (_state.sda != -1 && _state.scl != -1) {
            Wire.begin(_state.sda, _state.scl);
            i2c_active = true;
        }
    }

   private:
    PsychicHttpServer *_server;
    SecurityManager *_securityManager;
    HttpEndpoint<DeviceConfiguration> _httpEndpoint;
    EventEndpoint<DeviceConfiguration> _eventEndpoint;
    FSPersistence<DeviceConfiguration> _fsPersistence;

    bool i2c_active = false;
};
