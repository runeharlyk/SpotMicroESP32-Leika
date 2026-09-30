#include <peripherals/peripherals.h>
#include <utils/sleep.h>

Peripherals::Peripherals()
    : protoHandler(PeripheralsConfiguration_read, PeripheralsConfiguration_update, this),
      _persistence(PeripheralsConfiguration_read, PeripheralsConfiguration_update, this, PERIPHERAL_SETTINGS_FILE,
                   api_PeripheralSettings_fields, api_PeripheralSettings_size, PeripheralsConfiguration_defaults()) {
    _accessMutex = xSemaphoreCreateRecursiveMutex();
    addUpdateHandler([&](const std::string &originId) { updatePins(); }, false);
}

void Peripherals::begin() {
    _persistence.readFromFS();

    updatePins();
}

void Peripherals::beginSensors() {
#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
    if (!_imu.initialize()) ESP_LOGE("Peripherals", "IMU initialize failed");
#endif
#if FT_ENABLED(USE_HMC5883)
    if (!_mag.initialize()) ESP_LOGE("Peripherals", "Magnetometer initialize failed");
#endif
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
};

void Peripherals::update() {
    EXECUTE_EVERY_N_MS(20, { readImu(); });
    EXECUTE_EVERY_N_MS(100, { readMag(); });
    EXECUTE_EVERY_N_MS(100, { readGesture(); });
    EXECUTE_EVERY_N_MS(500, { readBMP(); });
    EXECUTE_EVERY_N_MS(500, { readSonar(); });
}

void Peripherals::updatePins() {
    if (_i2c_active) {
        I2CBus::instance().end();
    }

    if (state().sda != -1 && state().scl != -1) {
        esp_err_t err = I2CBus::instance().begin(static_cast<gpio_num_t>(state().sda),
                                                 static_cast<gpio_num_t>(state().scl), state().frequency);
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
    data.x = _readings.angleX;
    data.y = _readings.angleY;
    data.z = _readings.angleZ;
    data.heading = _readings.heading;
    data.altitude = _readings.altitude;
    data.bmp_temp = _readings.temperature;
    data.pressure = _readings.pressure;
}

void Peripherals::readImu() {
#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
    beginTransaction();
    if (_imu.update()) {
        std::lock_guard<std::mutex> lock(_readingsMutex);
        _readings.angleX = _imu.getAngleX();
        _readings.angleY = _imu.getAngleY();
        _readings.angleZ = _imu.getAngleZ();
#if !FT_ENABLED(USE_HMC5883)
        _readings.heading = _imu.getAngleZ();
#endif
    }
    endTransaction();
#endif
}

void Peripherals::readMag() {
#if FT_ENABLED(USE_HMC5883)
    beginTransaction();
    if (_mag.update()) {
        std::lock_guard<std::mutex> lock(_readingsMutex);
        _readings.heading = _mag.getHeading();
    }
    endTransaction();
#endif
}

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

float Peripherals::angleX() {
    std::lock_guard<std::mutex> lock(_readingsMutex);
    return _readings.angleX;
}

float Peripherals::angleY() {
    std::lock_guard<std::mutex> lock(_readingsMutex);
    return _readings.angleY;
}

float Peripherals::angleZ() {
    std::lock_guard<std::mutex> lock(_readingsMutex);
    return _readings.angleZ;
}

gesture_t Peripherals::takeGesture() {
    std::lock_guard<std::mutex> lock(_readingsMutex);
    const gesture_t gesture = _readings.gesture;
    _readings.gesture = eGestureNone;
    return gesture;
}

float Peripherals::leftDistance() {
    std::lock_guard<std::mutex> lock(_readingsMutex);
    return _readings.leftDistance;
}

float Peripherals::rightDistance() {
    std::lock_guard<std::mutex> lock(_readingsMutex);
    return _readings.rightDistance;
}

bool Peripherals::calibrateIMU() {
#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
    beginTransaction();
    bool result = _imu.calibrate();
    endTransaction();
    return result;
#else
    return false;
#endif
}