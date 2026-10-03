#pragma once

#include <template/stateful_persistence.h>
#include <template/stateful_service.h>
#include <template/stateful_proto_handler.h>
#include <utils/math_utils.h>
#include <utils/timing.h>
#include <filesystem.h>
#include <settings/peripherals_settings.h>
#include <platform_shared/message.pb.h>

#include <deque>
#include <functional>
#include <list>
#include <mutex>

#include <peripherals/i2c_bus.h>
#include <peripherals/imu/imu.h>
#include <peripherals/drivers/mpu6050.h>
#include <peripherals/drivers/bno055.h>
#include <peripherals/drivers/icm20948.h>
#include <peripherals/drivers/hmc5883l.h>
#include <peripherals/barometer.h>
#include <peripherals/gesture.h>

/**
 * The sensors' latest values. The sensor task writes them; the control and service tasks copy them out
 * under a lock held only for the copy, so a slow or absent sensor never stalls the control loop.
 */
struct SensorReadings {
    ImuSample imu;
    float altitude {0};
    float temperature {0};
    float pressure {0};
    gesture_t gesture {eGestureNone};
};

/** What the last probe found: a sensor is detected when it answered, and active when it is also not disabled. */
struct SensorStatus {
    bool imuDetected = false, imuActive = false;
    bool magDetected = false, magActive = false;
    bool bmpDetected = false, bmpActive = false;
    bool gestureDetected = false, gestureActive = false;
    const char *imuDriver = "none";
    uint32_t imuRateHz = 0, magRateHz = 0;
};

class Peripherals : public StatefulService<PeripheralsConfiguration> {
  public:
    Peripherals();

    // Loads the settings and starts the I2C bus, which the servos need too.
    void begin();

    // Probes the sensors and starts those not disabled, which can take seconds: call from the sensor task, as
    // sensorTick(). A change of what is disabled probes again there.
    void beginSensors();

    SensorStatus status() const;

    // One pass of the sensor task: queued bus work, the IMU every time, the slower sensors when due.
    void sensorTick();

    void updatePins();

    void scanI2C(uint8_t lower = 1, uint8_t higher = 127);

    void getI2CScanProto(socket_message_I2CScanData &data);
    void getIMUProto(socket_message_IMUData &data);

    ImuSample imuSample();
    const char *imuDriverName() const;
    uint32_t imuRateHz() const;
    uint32_t magRateHz() const;

    gesture_t takeGesture();

    struct ImuCalibration {
        bool still = false;     // the gyro bias was taken
        bool levelled = false;  // the tilt was folded into the stored mounting
        float tiltDeg = 0;      // the tilt the accelerometer showed, when levelling was asked for
    };

    /**
     * The gyro bias of a still robot; with `level`, also its tilt folded into the stored mounting, so a robot lying
     * on a level surface reads level. Runs on the sensor task; the robot must stay still for about a second.
     */
    ImuCalibration calibrateIMU(bool level);

    /**
     * Queues bus work too slow for the socket task, such as a scan or a calibration, to run on the sensor task
     * between reads. Refuses when the queue is full.
     */
    bool runOnSensorTask(std::function<void()> work);

    StatefulProtoHandler<PeripheralsConfiguration, api_PeripheralSettings> protoHandler;

  private:
    struct SensorOptions {
        bool imuDisabled, magDisabled, bmpDisabled, gestureDisabled;
        bool operator==(const SensorOptions &) const = default;
    };
    SensorOptions sensorOptions() const;
    void probeSensors(const SensorOptions &options);
    SensorOptions _probedOptions {};

    void readImu();
    ImuConfig imuConfig() const;
    void readBMP();
    void readGesture();

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

    ICM20948Driver _icm;
    BNO055Driver _bno;
    MPU6050Driver _mpu;
    HMC5883LDriver _hmc;
    Imu _imu;
    Barometer _bmp;
    GestureSensor _gesture;

    mutable std::mutex _statusMutex;
    SensorStatus _status;

    std::list<uint8_t> _address_list;
    bool _i2c_active = false;
};