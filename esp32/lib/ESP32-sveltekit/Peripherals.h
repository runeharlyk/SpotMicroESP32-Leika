#ifndef Peripherals_h
#define Peripherals_h

#include <EventEndpoint.h>
#include <stateful_persistence.h>
#include <stateful_service.h>
#include <MathUtils.h>
#include <utils/timing.h>
#include <filesystem.h>
#include <features.h>
#include <settings/peripherals_settings.h>
#include <stateful_service_endpoint.h>

#include <list>
#include <SPI.h>
#include <Wire.h>

#include <Adafruit_PWMServoDriver.h>
#include <Adafruit_BMP085_U.h>
#include <Adafruit_HMC5883_U.h>
#include <Adafruit_Sensor.h>
#include <NewPing.h>
#include <peripherals/imu.h>

#define EVENT_CONFIGURATION_SETTINGS "peripheralSettings"

#define EVENT_I2C_SCAN "i2cScan"

#define I2C_INTERVAL 250
#define MAX_ESP_IMU_SIZE 500
#define EVENT_IMU "imu"
#define EVENT_SERVO_STATE "servoState"

/*
 * Servo Settings
 */
#ifndef FACTORY_SERVO_PWM_FREQUENCY
#define FACTORY_SERVO_PWM_FREQUENCY 50
#endif

#ifndef FACTORY_SERVO_OSCILLATOR_FREQUENCY
#define FACTORY_SERVO_OSCILLATOR_FREQUENCY 27000000
#endif

/*
 * OLED Settings
 */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_RESET -1

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
#if FT_ENABLED(USE_MAG)
          _mag(12345),
#endif
#if FT_ENABLED(USE_BMP)
          _bmp(10085),
#endif
          _persistence(PeripheralsConfiguration::read, PeripheralsConfiguration::update, this, DEVICE_CONFIG_FILE) {
        _accessMutex = xSemaphoreCreateMutex();
        addUpdateHandler([&](const String &originId) { updatePins(); }, false);
    };

    void begin() {
        _eventEndpoint.begin();
        _persistence.readFromFS();

        socket.onEvent(EVENT_I2C_SCAN, [&](JsonObject &root, int originId) {
            scanI2C();
            emitI2C();
        });

        socket.onSubscribe(EVENT_I2C_SCAN, [&](const String &originId, bool sync) {
            scanI2C();
            emitI2C(originId, sync);
        });

        updatePins();

#if FT_ENABLED(USE_SERVO)
        _pca.begin();
        _pca.setPWMFreq(FACTORY_SERVO_PWM_FREQUENCY);
        _pca.setOscillatorFrequency(FACTORY_SERVO_OSCILLATOR_FREQUENCY);
        _pca.sleep();
        socket.onEvent(EVENT_SERVO_STATE, [&](JsonObject &root, int originId) {
            _pca_active = root["active"] | false;
            _pca_active ? pcaActivate() : pcaDeactivate();
        });
#endif

#if FT_ENABLED(USE_IMU)
        if (!_imu.initialize()) ESP_LOGE("IMUService", "IMU initialize failed");
#endif
#if FT_ENABLED(USE_MAG)
        mag_success = _mag.begin();
        if (!mag_success) {
            ESP_LOGE("IMUService", "MAG initialize failed");
        }
#endif
#if FT_ENABLED(USE_BMP)
        bmp_success = _bmp.begin();
        if (!bmp_success) {
            ESP_LOGE("IMUService", "BMP initialize failed");
        }
#endif

#if FT_ENABLED(USE_USS)
        _left_sonar = new NewPing(USS_LEFT_PIN, USS_LEFT_PIN, MAX_DISTANCE);
        _right_sonar = new NewPing(USS_RIGHT_PIN, USS_RIGHT_PIN, MAX_DISTANCE);
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

        if (_state.sda != -1 && _state.scl != -1) {
            Wire.begin(_state.sda, _state.scl, _state.frequency);
            i2c_active = true;
        }
    }

    void emitI2C(const String &originId = "", bool sync = false) {
        char output[150];
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        root["sda"] = _state.sda;
        root["scl"] = _state.scl;
        JsonArray addresses = root["addresses"].to<JsonArray>();
        for (auto &address : addressList) {
            addresses.add(address);
        }
        serializeJson(root, output);
        ESP_LOGI("Peripherals", "Emitting I2C scan results, %s %d", originId.c_str(), sync);
        socket.emit(EVENT_I2C_SCAN, output, originId.c_str(), sync);
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
        if (nDevices == 0)
            ESP_LOGI("Peripherals", "No I2C devices found");
        else
            ESP_LOGI("Peripherals", "Scan complete - Found %d devices", nDevices);
    }

    /* SERVO FUNCTIONS*/
    void pcaLerpTo(int index, int value, float t) {
#if FT_ENABLED(USE_SERVO)
        target_pwm[index] = lerp(pwm[index], value, t);
#endif
    }

    void pcaWrite(int index, int value) {
#if FT_ENABLED(USE_SERVO)
        if (value < 0 || value > 4096) {
            ESP_LOGE("Peripherals", "Invalid PWM value %d for %d :: Valid range 0-4096", value, index);
            return;
        }
        pwm[index] = value;
        target_pwm[index] = value;
        beginTransaction();
        _pca.setPWM(index, 0, value);
        endTransaction();
#endif
    }

    void pcaWriteAngle(int index, int angle) {
#if FT_ENABLED(USE_SERVO)
        _pca.setPWM(index, 0, 125 + angle * 2);
#endif
    }

    void pcaWriteAngles(float *angles, int numServos, int offset = 0) {
#if FT_ENABLED(USE_SERVO)
        for (int i = 0; i < numServos; i++) {
            pcaWriteAngle(i + offset, angles[i]);
        }
#endif
    }

    void pcaActivate() {
#if FT_ENABLED(USE_SERVO)
        if (_pca_active) return;
        _pca_active = true;
        _pca.wakeup();
#endif
    }

    void pcaDeactivate() {
#if FT_ENABLED(USE_SERVO)
        if (!_pca_active) return;
        _pca_active = false;
        _pca.sleep();
#endif
    }

    /* IMU FUNCTIONS */
    bool readIMU() {
        bool updated = false;
#if FT_ENABLED(USE_IMU)
        beginTransaction();
        updated = _imu.readIMU();
        endTransaction();
#endif
        return updated;
    }

    /* MAG FUNCTIONS */
    float getHeading() {
        float heading = 0;
#if FT_ENABLED(USE_MAG)
        sensors_event_t event;
        _mag.getEvent(&event);
        heading = atan2(event.magnetic.y, event.magnetic.x);
        float declinationAngle = 0.22;
        heading += declinationAngle;
        if (heading < 0) heading += 2 * PI;
        if (heading > 2 * PI) heading -= 2 * PI;
        heading *= 180 / M_PI;
#endif
        return heading;
    }

    /* BMP FUNCTIONS */
    float getAltitude() {
        float altitude = -1;
#if FT_ENABLED(USE_MAG)
        sensors_event_t event;
        _bmp.getEvent(&event);
        float seaLevelPressure = SENSORS_PRESSURE_SEALEVELHPA;
        altitude = bmp_success && event.pressure ? _bmp.pressureToAltitude(seaLevelPressure, event.pressure) : -1;
#endif
        return altitude;
    }

    float getPressure() {
        float pressure = -1;
#if FT_ENABLED(USE_BMP)
        sensors_event_t event;
        _bmp.getEvent(&event);
        pressure = bmp_success && event.pressure ? event.pressure : -1;
#endif
        return pressure;
    }

    float getTemperature() {
        float temperature = 0;
#if FT_ENABLED(USE_BMP)
        _bmp.getTemperature(&temperature);
        temperature = bmp_success ? temperature : -1;
#endif
        return temperature;
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
#if FT_ENABLED(USE_IMU)
        _imu.readIMU(root);
#endif
#if FT_ENABLED(USE_MAG)
        if (mag_success) {
            doc["heading"] = round2(getHeading());
        }
#endif
#if FT_ENABLED(USE_BMP)
        if (bmp_success) {
            doc["pressure"] = round2(getPressure());
            doc["altitude"] = round2(getAltitude());
            doc["bmp_temp"] = round2(getTemperature());
        }
#endif
        serializeJson(doc, message);
        socket.emit(EVENT_IMU, message);
    }

    void emitSonar() {
#if FT_ENABLED(USE_USS)

        char output[16];
        snprintf(output, sizeof(output), "[%.1f,%.1f]", _left_distance, _right_distance);
        socket.emit("sonar", output);
#endif
    }

  private:
    EventEndpoint<PeripheralsConfiguration> _eventEndpoint;
    FSPersistence<PeripheralsConfiguration> _persistence;

    SemaphoreHandle_t _accessMutex;
    inline void beginTransaction() { xSemaphoreTakeRecursive(_accessMutex, portMAX_DELAY); }

    inline void endTransaction() { xSemaphoreGiveRecursive(_accessMutex); }

    JsonDocument doc;
    char message[MAX_ESP_IMU_SIZE];

#if FT_ENABLED(USE_SERVO)
    Adafruit_PWMServoDriver _pca;
    bool _pca_active {false};
    uint16_t pwm[16] = {0};
    uint16_t target_pwm[16] = {0};
#endif
#if FT_ENABLED(USE_IMU)
    IMU _imu;
#endif
#if FT_ENABLED(USE_MAG)
    Adafruit_HMC5883_Unified _mag;
    bool mag_success {false};
#endif
#if FT_ENABLED(USE_BMP)
    Adafruit_BMP085_Unified _bmp;
    bool bmp_success {false};
#endif
#if FT_ENABLED(USE_USS)
    NewPing *_left_sonar;
    NewPing *_right_sonar;
#endif
    float _left_distance {MAX_DISTANCE};
    float _right_distance {MAX_DISTANCE};

    std::list<uint8_t> addressList;
    bool i2c_active = false;
    unsigned long _updateInterval {I2C_INTERVAL};
};

#endif