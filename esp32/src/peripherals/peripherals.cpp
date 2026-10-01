#include <peripherals/peripherals.h>
#include <utils/sleep.h>
#include <esp_timer.h>
#include <cmath>

#if FT_ENABLED(USE_MPU6050 || USE_ICM20948 || USE_BNO055)
#define IMU_DRIVER &_imuDriver
#else
#define IMU_DRIVER nullptr
#endif
#if FT_ENABLED(USE_HMC5883)
#define MAG_DRIVER &_magDriver
#else
#define MAG_DRIVER nullptr
#endif

Peripherals::Peripherals()
    : protoHandler(PeripheralsConfiguration_read, PeripheralsConfiguration_update, this, api_PeripheralSettings_fields),
      _persistence(PeripheralsConfiguration_read, PeripheralsConfiguration_update, this, PERIPHERAL_SETTINGS_FILE,
                   api_PeripheralSettings_fields, api_PeripheralSettings_size, PeripheralsConfiguration_defaults()),
      _imu(IMU_DRIVER, MAG_DRIVER) {
    _accessMutex = xSemaphoreCreateRecursiveMutex();
    addUpdateHandler(
        [&](const std::string &originId) {
            updatePins();
            runOnSensorTask([this] { _imu.configure(imuConfig()); });
        },
        false);
}

void Peripherals::begin() {
    _persistence.readFromFS();

    updatePins();
}

void Peripherals::beginSensors() {
    beginTransaction();
    _imu.configure(imuConfig());
    if (!_imu.begin(esp_timer_get_time())) ESP_LOGW("Peripherals", "No IMU answered");
    else ESP_LOGI("Peripherals", "IMU %s at %u Hz, compass at %u Hz", _imu.driverName(), _imu.rateHz(), _imu.magRateHz());
    endTransaction();
#if FT_ENABLED(USE_BMP180)
    if (!_bmp.initialize()) ESP_LOGE("Peripherals", "Barometer initialize failed");
#endif
#if FT_ENABLED(USE_PAJ7620U2)
    if (!_gesture.initialize()) ESP_LOGE("Peripherals", "Gesture sensor initialize failed");
#endif
#if FT_ENABLED(USE_USS)
    _left_sonar = std::make_unique<NewPing>(USS_LEFT_PIN, USS_LEFT_PIN, MAX_DISTANCE);
    _right_sonar = std::make_unique<NewPing>(USS_RIGHT_PIN, USS_RIGHT_PIN, MAX_DISTANCE);
#endif
}

void Peripherals::sensorTick() {
    runQueuedWork();
    readImu();
    EXECUTE_EVERY_N_MS(100, { readGesture(); });
    EXECUTE_EVERY_N_MS(500, { readBMP(); });
    EXECUTE_EVERY_N_MS(500, { readSonar(); });
}

void Peripherals::updatePins() {
    if (_i2c_active) {
        I2CBus::instance().end();
    }

    const PeripheralsConfiguration settings = snapshot();
    if (settings.sda != -1 && settings.scl != -1) {
        esp_err_t err = I2CBus::instance().begin(static_cast<gpio_num_t>(settings.sda),
                                                 static_cast<gpio_num_t>(settings.scl), settings.frequency);
        _i2c_active = (err == ESP_OK);
    }
}

void Peripherals::getI2CScanProto(socket_message_I2CScanData &data) {
    data.devices_count = 0;
    for (auto &address : _address_list) {
        if (data.devices_count >= 16) break;
        data.devices[data.devices_count].address = address;
        data.devices_count++;
    }
}

void Peripherals::scanI2C(uint8_t lower, uint8_t higher) {
    _address_list.clear();
    auto devices = I2CBus::instance().scan(lower, higher);
    for (auto addr : devices) {
        _address_list.emplace_back(addr);
        ESP_LOGI("Peripherals", "I2C device found at address 0x%02X", addr);
    }
    ESP_LOGI("Peripherals", "Scan complete - Found %d device(s)", devices.size());
}

void Peripherals::getIMUProto(socket_message_IMUData &data) {
    std::lock_guard<std::mutex> lock(_readingsMutex);
    data.x = _readings.imu.rpy[0];
    data.y = _readings.imu.rpy[1];
    data.z = _readings.imu.rpy[2];
    // The app's compass shows degrees clockwise from north; yaw turns the other way.
    data.heading = std::fmod(360.0f - RAD_TO_DEG_F(_readings.imu.rpy[2]) + 360.0f, 360.0f);
    data.altitude = _readings.altitude;
    data.bmp_temp = _readings.temperature;
    data.pressure = _readings.pressure;
}

void Peripherals::readImu() {
    ImuSample sample;
    beginTransaction();
    const bool fresh = _imu.update(esp_timer_get_time(), sample);
    endTransaction();
    if (!fresh) return;
    std::lock_guard<std::mutex> lock(_readingsMutex);
    _readings.imu = sample;
}

// Only the IMU part: the whole settings are 1.8 KB, too much for the sensor task's stack.
ImuConfig Peripherals::imuConfig() const {
    ImuConfig config;
    read([&config](const PeripheralsConfiguration &settings) { config = imuConfigFrom(effectiveImuSettings(settings)); });
    return config;
}

ImuSample Peripherals::imuSample() {
    std::lock_guard<std::mutex> lock(_readingsMutex);
    return _readings.imu;
}

const char *Peripherals::imuDriverName() const { return _imu.driverName(); }
uint32_t Peripherals::imuRateHz() const { return _imu.rateHz(); }
uint32_t Peripherals::magRateHz() const { return _imu.magRateHz(); }

void Peripherals::readBMP() {
#if FT_ENABLED(USE_BMP180)
    beginTransaction();
    if (_bmp.update()) {
        std::lock_guard<std::mutex> lock(_readingsMutex);
        _readings.altitude = _bmp.getAltitude();
        _readings.temperature = _bmp.getTemperature();
        _readings.pressure = _bmp.getPressure();
    }
    endTransaction();
#endif
}

void Peripherals::readGesture() {
#if FT_ENABLED(USE_PAJ7620U2)
    beginTransaction();
    if (_gesture.readGesture()) {
        std::lock_guard<std::mutex> lock(_readingsMutex);
        _readings.gesture = _gesture.getGesture();
    }
    endTransaction();
#endif
}

void Peripherals::readSonar() {
#if FT_ENABLED(USE_USS)
    const float left = _left_sonar->ping_cm();
    // Lets the left ping's echo die out before the right one listens.
    sleepAtLeastMs(50);
    const float right = _right_sonar->ping_cm();
    std::lock_guard<std::mutex> lock(_readingsMutex);
    _readings.leftDistance = left;
    _readings.rightDistance = right;
#endif
}

gesture_t Peripherals::takeGesture() {
    std::lock_guard<std::mutex> lock(_readingsMutex);
    const gesture_t gesture = _readings.gesture;
    _readings.gesture = eGestureNone;
    return gesture;
}

bool Peripherals::runOnSensorTask(std::function<void()> work) {
    std::lock_guard<std::mutex> lock(_workMutex);
    if (_work.size() >= MAX_QUEUED_WORK) return false;
    _work.push_back(std::move(work));
    return true;
}

void Peripherals::runQueuedWork() {
    for (;;) {
        std::function<void()> work;
        {
            std::lock_guard<std::mutex> lock(_workMutex);
            if (_work.empty()) return;
            work = std::move(_work.front());
            _work.pop_front();
        }
        work();
    }
}

Peripherals::ImuCalibration Peripherals::calibrateIMU(bool level) {
    ImuCalibration result;
    Vec3 up {};
    beginTransaction();
    result.still = _imu.estimateGyroBias(
        [] {
            sleepAtLeastMs(5);
            return esp_timer_get_time();
        },
        level ? &up : nullptr);
    endTransaction();
    if (!level || !result.still) return result;
    // Saving reconfigures the IMU through the update handler.
    update(
        [&](PeripheralsConfiguration &settings) {
            api_ImuSettings imu = effectiveImuSettings(settings);
            if (!levelImuSettings(imu, up, result.tiltDeg)) return StateUpdateResult::UNCHANGED;
            settings.has_imu = true;
            settings.imu = imu;
            result.levelled = true;
            return StateUpdateResult::CHANGED;
        },
        "imu-level");
    return result;
}
