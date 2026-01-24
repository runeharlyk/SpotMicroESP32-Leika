#pragma once

#include <template/stateful_persistence.h>
#include <template/stateful_service.h>
#include <utils/math_utils.h>
#include <utils/timing.h>
#include <filesystem.h>
#include <features.h>
#include <settings/peripherals_settings.h>
#include <template/stateful_endpoint.h>
#include <platform_shared/message.pb.h>

#include <list>
#include <SPI.h>

#include <NewPing.h>
#include <peripherals/i2c_bus.h>
#include <peripherals/imu.h>
#include <peripherals/magnetometer.h>
#include <peripherals/barometer.h>
#include <peripherals/gesture.h>

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

    void getI2CScanProto(socket_message_I2CScanData &data);
    void getIMUProto(socket_message_IMUData &data);
    void getSettingsProto(socket_message_PeripheralSettingsData &data);

    /* IMU FUNCTIONS */
    bool readImu();

    bool readMag();

    bool readBMP();

    bool readGesture();

    void readSonar();

    float angleX();

    float angleY();

    float angleZ();

    gesture_t takeGesture();

    float leftDistance();
    float rightDistance();

    bool calibrateIMU();

    StatefulHttpEndpoint<PeripheralsConfiguration> endpoint;

  private:
    FSPersistence<PeripheralsConfiguration> _persistence;

    SemaphoreHandle_t _accessMutex;
    inline void beginTransaction() { xSemaphoreTakeRecursive(_accessMutex, portMAX_DELAY); }

    inline void endTransaction() { xSemaphoreGiveRecursive(_accessMutex); }

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
};