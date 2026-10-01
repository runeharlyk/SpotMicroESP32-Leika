#pragma once

#include <template/stateful_persistence.h>
#include <template/stateful_service.h>
#include <template/stateful_proto_handler.h>
#include <utils/math_utils.h>
#include <utils/timing.h>
#include <filesystem.h>
#include <features.h>
#include <settings/peripherals_settings.h>
#include <platform_shared/message.pb.h>

#include <deque>
#include <functional>
#include <list>
#include <mutex>

#if FT_ENABLED(USE_USS)
#include <NewPing.h>
#endif
#include <peripherals/i2c_bus.h>
#include <peripherals/imu.h>
#include <peripherals/magnetometer.h>
#include <peripherals/barometer.h>
#include <peripherals/gesture.h>

/*
 * Ultrasonic Sensor Settings
 */
#define MAX_DISTANCE 200

/**
 * The sensors' latest values. The sensor task writes them; the control and service tasks copy them out
 * under a lock held only for the copy, so a slow or absent sensor never stalls the control loop.
 */
struct SensorReadings {
    float angleX {0};
    float angleY {0};
    float angleZ {0};
    float heading {0};
    float altitude {0};
    float temperature {0};
    float pressure {0};
    float leftDistance {MAX_DISTANCE};
    float rightDistance {MAX_DISTANCE};
    gesture_t gesture {eGestureNone};
};

class Peripherals : public StatefulService<PeripheralsConfiguration> {
  public:
    Peripherals();

    // Loads the settings and starts the I2C bus, which the servos need too.
    void begin();

    // Brings up the sensors, which can take seconds: call from the sensor task, as update().
    void beginSensors();

    void update();

    void updatePins();

    void scanI2C(uint8_t lower = 1, uint8_t higher = 127);

    void getI2CScanProto(socket_message_I2CScanData &data);
    void getIMUProto(socket_message_IMUData &data);

    float angleX();

    float angleY();

    float angleZ();

    gesture_t takeGesture();

    bool calibrateIMU();

    /**
     * Queues bus work too slow for the socket task, such as a scan or a calibration, to run on the sensor task
     * between reads. Refuses when the queue is full.
     */
    bool runOnSensorTask(std::function<void()> work);

    StatefulProtoHandler<PeripheralsConfiguration, api_PeripheralSettings> protoHandler;

  private:
    void readImu();
    void readMag();
    void readBMP();
    void readGesture();
    void readSonar();

    FSPersistencePB<PeripheralsConfiguration> _persistence;

    std::mutex _readingsMutex;
    SensorReadings _readings;

    static constexpr size_t MAX_QUEUED_WORK = 4;
    std::mutex _workMutex;
    std::deque<std::function<void()>> _work;
    void runQueuedWork();

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

    std::list<uint8_t> _address_list;
    bool _i2c_active = false;
};