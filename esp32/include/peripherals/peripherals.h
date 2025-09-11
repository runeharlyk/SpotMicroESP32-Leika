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
#include <peripherals/barometer.h>
#include <peripherals/gesture.h>

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
    Peripherals();

    void begin();

    void update();

    void updatePins();

    void scanI2C(uint8_t lower = 1, uint8_t higher = 127);

    void getI2CResult(JsonVariant &root);

    void getIMUResult(JsonVariant &root);

    void getSonarResult(JsonVariant &root);

    /* IMU FUNCTIONS */
    bool readImu();

    bool readMag();

    bool readBMP();

    bool readGesture();

    void readSonar();

    float angleX();

    float angleY();

    float angleZ();

    gesture_t const takeGesture();

    float leftDistance();
    float rightDistance();

    StatefulHttpEndpoint<PeripheralsConfiguration> endpoint;

  private:
    EventEndpoint<PeripheralsConfiguration> _eventEndpoint;
    FSPersistence<PeripheralsConfiguration> _persistence;

    SemaphoreHandle_t _accessMutex;
    inline void beginTransaction() { xSemaphoreTakeRecursive(_accessMutex, portMAX_DELAY); }

    inline void endTransaction() { xSemaphoreGiveRecursive(_accessMutex); }

    JsonDocument doc;
    char message[MAX_ESP_IMU_SIZE];
#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
    IMU _imu;
#endif
#if FT_ENABLED(USE_HMC5883)
    Magnetometer _mag;
#endif
#if FT_ENABLED(USE_BMP180)
    Barometer _bmp;
#endif
#if FT_ENABLED(USE_PAJ7620U2)
    GestureSensor _gesture;
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