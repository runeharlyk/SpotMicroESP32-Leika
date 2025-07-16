#ifndef Peripherals_h
#define Peripherals_h

#include <template/stateful_socket.h>
#include <template/stateful_persistence.h>
#include <template/stateful_service.h>
#include <utils/math_utils.h>
#include <utils/timing.h>
#include <filesystem.h>
#include <features.h>
#include <settings/peripherals_settings.h>
#include <template/stateful_endpoint.h>

#include <list>
#include <SPI.h>
#include <Wire.h>

#include <NewPing.h>
#include <peripherals/imu.h>
#include <peripherals/magnetometer.h>

#define EVENT_CONFIGURATION_SETTINGS "peripheralSettings"

#define EVENT_I2C_SCAN "i2cScan"

#define I2C_INTERVAL 250
#define MAX_ESP_IMU_SIZE 500
#define EVENT_IMU "imu"

/*
 * Ultrasonic Sensor Settings
 */
#define MAX_DISTANCE 200

class Peripherals : public StatefulService<PeripheralsConfiguration> {
  public:
    Peripherals()
        : endpoint(PeripheralsConfiguration::read, PeripheralsConfiguration::update, this),
          _eventEndpoint(PeripheralsConfiguration::read, PeripheralsConfiguration::update, this,
                         EVENT_CONFIGURATION_SETTINGS),
          _persistence(PeripheralsConfiguration::read, PeripheralsConfiguration::update, this, DEVICE_CONFIG_FILE) {
        _accessMutex = xSemaphoreCreateMutex();
        addUpdateHandler([&](const String &originId) { updatePins(); }, false);
    };

    void begin() {
        _eventEndpoint.begin();
        _persistence.readFromFS();

        socket.onEvent(EVENT_I2C_SCAN, [&](JsonVariant &root, int originId) {
            scanI2C();
            emitI2C();
        });

        socket.onSubscribe(EVENT_I2C_SCAN, [&](const String &originId, bool sync) {
            scanI2C();
            emitI2C(originId, sync);
        });

        updatePins();

#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
        if (!_imu.initialize()) ESP_LOGE("IMUService", "IMU initialize failed");
#endif
#if FT_ENABLED(USE_HMC5883)
        if (!_mag.initialize()) ESP_LOGE("IMUService", "MAG initialize failed");
#endif
#if FT_ENABLED(USE_USS)
        _left_sonar = std::make_unique<NewPing>(USS_LEFT_PIN, USS_LEFT_PIN, MAX_DISTANCE);
        _left_sonar = std::make_unique<NewPing>(USS_RIGHT_PIN, USS_RIGHT_PIN, MAX_DISTANCE);
#endif
    };

    void loop() {
        EXECUTE_EVERY_N_MS(_updateInterval, {
            beginTransaction();
            emitIMU();
            readSonar();
            emitSonar();
            endTransaction();
        });
    }

    void updatePins() {
        if (i2c_active) {
            Wire.end();
        }

        if (state().sda != -1 && state().scl != -1) {
            Wire.begin(state().sda, state().scl, state().frequency);
            i2c_active = true;
        }
    }

    void emitI2C(const String &originId = "", bool sync = false) {
        char output[150];
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        root["sda"] = state().sda;
        root["scl"] = state().scl;
        JsonArray addresses = root["addresses"].to<JsonArray>();
        for (auto &address : addressList) {
            addresses.add(address);
        }
        ESP_LOGI("Peripherals", "Emitting I2C scan results, %s %d", originId.c_str(), sync);
        JsonVariant data = doc.as<JsonVariant>();
        socket.emit(EVENT_I2C_SCAN, data, originId.c_str(), sync);
    }

    void scanI2C(uint8_t lower = 1, uint8_t higher = 127) {
        addressList.clear();
        for (uint8_t address = lower; address < higher; address++) {
            Wire.beginTransmission(address);
            if (Wire.endTransmission() == 0) {
                addressList.emplace_back(address);
                ESP_LOGI("Peripherals", "I2C device found at address 0x%02X", address);
            }
        }
        uint8_t nDevices = addressList.size();
        ESP_LOGI("Peripherals", "Scan complete - Found %d device(s)", nDevices);
    }

    /* IMU FUNCTIONS */
    bool readIMU() {
        bool updated = false;
#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
        beginTransaction();
        updated = _imu.readIMU();
        endTransaction();
#endif
        return updated;
    }

    bool readMag() {
        bool updated = false;
#if FT_ENABLED(USE_HMC5883)
        beginTransaction();
        updated = _mag.readMagnetometer();
        endTransaction();
#endif
        return updated;
    }

    void readSonar() {
#if FT_ENABLED(USE_USS)
        _left_distance = _left_sonar->ping_cm();
        delay(50);
        _right_distance = _right_sonar->ping_cm();
#endif
    }

    float leftDistance() { return _left_distance; }
    float rightDistance() { return _right_distance; }

    StatefulHttpEndpoint<PeripheralsConfiguration> endpoint;

    void emitIMU() {
        doc.clear();
        JsonObject root = doc.to<JsonObject>();
#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
        _imu.readIMU(root);
#endif
#if FT_ENABLED(USE_HMC5883)
        _mag.readMagnetometer(root);
#endif
        JsonVariant data = doc.as<JsonVariant>();
#if FT_ENABLED(USE_MPU6050 || USE_BNO055) || FT_ENABLED(USE_HMC5883)
        socket.emit(EVENT_IMU, data);
#endif
    }

    void emitSonar() {
#if FT_ENABLED(USE_USS)
        doc.clear();
        JsonArray root = doc.to<JsonArray>();
        root[0] = _left_distance, root[1] = _right_distance;
        JsonVariant data = doc.as<JsonVariant>();
        socket.emit("sonar", data);
#endif
    }

  private:
    EventEndpoint<PeripheralsConfiguration> _eventEndpoint;
    FSPersistence<PeripheralsConfiguration> _persistence;

    SemaphoreHandle_t _accessMutex;
    inline void beginTransaction() { xSemaphoreTakeRecursive(_accessMutex, portMAX_DELAY); }

    inline void endTransaction() { xSemaphoreGiveRecursive(_accessMutex); }

    JsonDocument doc;
#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
    IMU _imu;
#endif
#if FT_ENABLED(USE_HMC5883)
    Magnetometer _mag;
#endif
#if FT_ENABLED(USE_USS)
    std::unique_ptr<NewPing> _left_sonar;
    std::unique_ptr<NewPing> _right_sonar;
#endif
    float _left_distance {MAX_DISTANCE};
    float _right_distance {MAX_DISTANCE};

    std::list<uint8_t> addressList;
    bool i2c_active = false;
    unsigned long _updateInterval {I2C_INTERVAL};
};

#endif