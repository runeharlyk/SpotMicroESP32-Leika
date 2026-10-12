#include <peripherals/peripherals.h>
#include <utils/sleep.h>
#include <esp_timer.h>
#include <cmath>

// The ICM-20948 before the MPU6050: both answer at 0x68, and starting an MPU6050 resets whatever chip is there.
Peripherals::Peripherals()
    : protoHandler(PeripheralsConfiguration_read, PeripheralsConfiguration_save, this, api_PeripheralSettings_fields),
      _persistence(PeripheralsConfiguration_read, PeripheralsConfiguration_update, this, PERIPHERAL_SETTINGS_FILE,
                   api_PeripheralSettings_fields, api_PeripheralSettings_size, PeripheralsConfiguration_defaults()),
      _imu({&_icm, &_bno, &_mpu}, &_hmc) {
    _accessMutex = xSemaphoreCreateRecursiveMutex();
    addUpdateHandler(
        [&](const std::string &originId) {
            updatePins();
            runOnSensorTask([this, sonars = sonarWiring()] {
                if (!(sonars == _appliedSonars)) applySonars(sonars);
                _imu.configure(imuConfig());
                // Only a change of what is disabled: probing again restarts the fusion and resets the BNO055.
                const SensorOptions options = sensorOptions();
                if (!(options == _probedOptions)) probeSensors(options);
            });
        },
        false);
}

void Peripherals::begin() {
    _persistence.readFromFS();

    updatePins();
}

void Peripherals::beginSensors() {
    _imu.configure(imuConfig());
    probeSensors(sensorOptions());
    applySonars(sonarWiring());
}

// Sonars on pins they cannot take stay off, as a strip on such a pin stays dark: a stored file is loaded as it is.
Peripherals::SonarWiring Peripherals::sonarWiring() const {
    SonarWiring wiring {};
    read([&wiring](const PeripheralsConfiguration &settings) {
        const std::array<api_SonarSettings, 2> sonars = effectiveSonarSettings(settings);
        if (!sonars[0].enabled && !sonars[1].enabled) return;
        if (const char *problem = sonarProblem(sonars, settings, effectiveLedSettings(settings))) {
            ESP_LOGW("Peripherals", "Sonars left off: %s", problem);
            return;
        }
        for (size_t i = 0; i < sonars.size(); i++) {
            wiring[i] = {sonars[i].enabled, static_cast<gpio_num_t>(sonars[i].trigger_pin),
                         static_cast<gpio_num_t>(sonars[i].echo_pin)};
        }
    });
    return wiring;
}

void Peripherals::applySonars(const SonarWiring &wiring) {
    _appliedSonars = wiring;
    _sonarPins.attach(wiring);
    _ranger.enable({_sonarPins.attached(0), _sonarPins.attached(1)});
    {
        std::lock_guard<std::mutex> lock(_readingsMutex);
        _readings.sonars = _ranger.readings();
    }
    std::lock_guard<std::mutex> lock(_statusMutex);
    _status.sonarActive = _sonarPins.attached(0) || _sonarPins.attached(1);
}

Peripherals::SensorOptions Peripherals::sensorOptions() const {
    SensorOptions options;
    read([&options](const PeripheralsConfiguration &settings) {
        options = {settings.imu_disabled, settings.mag_disabled, settings.bmp_disabled, settings.gesture_disabled};
    });
    return options;
}

static const char *describe(bool detected, bool active) { return active ? "active" : detected ? "disabled" : "absent"; }

// A disabled sensor is only identified, never configured: the chip is left as it is.
void Peripherals::probeSensors(const SensorOptions &options) {
    SensorStatus status;
    beginTransaction();
    if (options.imuDisabled) {
        _imu.stop();
        status.imuDetected = _icm.identify() || _bno.identify() || _mpu.identify();
    } else {
        status.imuActive = status.imuDetected = _imu.begin(esp_timer_get_time(), !options.magDisabled);
    }
    status.magActive = _imu.hasMag();
    status.magDetected = status.magActive || _imu.chipHasMag() || _hmc.identify();
    status.imuDriver = _imu.driverName();
    status.imuRateHz = _imu.rateHz();
    status.magRateHz = _imu.magRateHz();

    if (options.bmpDisabled) {
        _bmp.stop();
        status.bmpDetected = _bmp.identify();
    } else {
        status.bmpActive = status.bmpDetected = _bmp.initialize();
    }
    if (options.gestureDisabled) {
        _gesture.stop();
        status.gestureDetected = _gesture.identify();
    } else {
        status.gestureActive = status.gestureDetected = _gesture.initialize();
    }
    endTransaction();

    _probedOptions = options;
    {
        std::lock_guard<std::mutex> lock(_statusMutex);
        status.sonarActive = _status.sonarActive;
        _status = status;
    }
    ESP_LOGI("Peripherals", "IMU %s %s at %u Hz, compass %s at %u Hz, barometer %s, gesture sensor %s",
             status.imuDriver, describe(status.imuDetected, status.imuActive), status.imuRateHz,
             describe(status.magDetected, status.magActive), status.magRateHz,
             describe(status.bmpDetected, status.bmpActive), describe(status.gestureDetected, status.gestureActive));
}

SensorStatus Peripherals::status() const {
    std::lock_guard<std::mutex> lock(_statusMutex);
    return _status;
}

void Peripherals::sensorTick() {
    runQueuedWork();
    readImu();
    if (_ranger.tick(static_cast<uint32_t>(esp_timer_get_time()))) {
        std::lock_guard<std::mutex> lock(_readingsMutex);
        _readings.sonars = _ranger.readings();
    }
    EXECUTE_EVERY_N_MS(100, { readGesture(); });
    EXECUTE_EVERY_N_MS(500, { readBMP(); });
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

static socket_message_SonarStatus sonarStatusProto(sonar::Status status) {
    switch (status) {
        case sonar::Status::Distance: return socket_message_SonarStatus_SONAR_DISTANCE;
        case sonar::Status::NoEcho: return socket_message_SonarStatus_SONAR_NO_ECHO;
        case sonar::Status::NoSensor: return socket_message_SonarStatus_SONAR_NO_SENSOR;
        case sonar::Status::Off: break;
    }
    return socket_message_SonarStatus_SONAR_OFF;
}

void Peripherals::getSonarProto(socket_message_SonarData &data) {
    std::array<sonar::Reading, sonar::COUNT> sonars;
    {
        std::lock_guard<std::mutex> lock(_readingsMutex);
        sonars = _readings.sonars;
    }
    data.has_left = data.has_right = true;
    data.left = {sonarStatusProto(sonars[0].status), sonars[0].distanceCm};
    data.right = {sonarStatusProto(sonars[1].status), sonars[1].distanceCm};
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

const char *Peripherals::imuDriverName() const { return status().imuDriver; }
uint32_t Peripherals::imuRateHz() const { return status().imuRateHz; }
uint32_t Peripherals::magRateHz() const { return status().magRateHz; }

void Peripherals::readBMP() {
    beginTransaction();
    if (_bmp.update()) {
        std::lock_guard<std::mutex> lock(_readingsMutex);
        _readings.altitude = _bmp.getAltitude();
        _readings.temperature = _bmp.getTemperature();
        _readings.pressure = _bmp.getPressure();
    }
    endTransaction();
}

void Peripherals::readGesture() {
    beginTransaction();
    if (_gesture.readGesture()) {
        std::lock_guard<std::mutex> lock(_readingsMutex);
        _readings.gesture = _gesture.getGesture();
    }
    endTransaction();
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
