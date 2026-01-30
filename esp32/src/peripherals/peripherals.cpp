#include <peripherals/peripherals.h>

Peripherals::Peripherals()
    : protoEndpoint(PeripheralsConfiguration_read, PeripheralsConfiguration_update, this,
                    API_REQUEST_EXTRACTOR(peripheral_settings, api_PeripheralSettings),
                    API_RESPONSE_ASSIGNER(peripheral_settings, api_PeripheralSettings)),
      _persistence(PeripheralsConfiguration_read, PeripheralsConfiguration_update, this,
                   PERIPHERAL_SETTINGS_FILE, api_PeripheralSettings_fields, api_PeripheralSettings_size,
                   PeripheralsConfiguration_defaults()) {
    _accessMutex = xSemaphoreCreateMutex();
    addUpdateHandler([&](const std::string &originId) { updatePins(); }, false);
}

void Peripherals::begin() {
    _persistence.readFromFS();

    updatePins();

#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
    if (!_imu.initialize()) ESP_LOGE("IMUService", "IMU initialize failed");
#endif
#if FT_ENABLED(USE_HMC5883)
    if (!_mag.initialize()) ESP_LOGE("IMUService", "MAG initialize failed");
#endif
#if FT_ENABLED(USE_BMP180)
    if (!_bmp.initialize()) ESP_LOGE("IMUService", "BMP initialize failed");
#endif
#if FT_ENABLED(USE_PAJ7620U2)
    if (!_gesture.initialize()) ESP_LOGE("IMUService", "Gesture initialize failed");
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
    if (i2c_active) {
        I2CBus::instance().end();
    }

    if (state().sda != -1 && state().scl != -1) {
        esp_err_t err = I2CBus::instance().begin(static_cast<gpio_num_t>(state().sda),
                                                 static_cast<gpio_num_t>(state().scl), state().frequency);
        i2c_active = (err == ESP_OK);
    }
}

void Peripherals::getI2CScanProto(socket_message_I2CScanData &data) {
    data.devices_count = 0;
    for (auto &address : addressList) {
        if (data.devices_count >= 16) break;
        data.devices[data.devices_count].address = address;
        data.devices_count++;
    }
}

void Peripherals::scanI2C(uint8_t lower, uint8_t higher) {
    addressList.clear();
    auto devices = I2CBus::instance().scan(lower, higher);
    for (auto addr : devices) {
        addressList.emplace_back(addr);
        ESP_LOGI("Peripherals", "I2C device found at address 0x%02X", addr);
    }
    ESP_LOGI("Peripherals", "Scan complete - Found %d device(s)", devices.size());
}

void Peripherals::getIMUProto(socket_message_IMUData &data) {
#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
    data.x = _imu.getAngleX();
    data.y = _imu.getAngleY();
    data.z = _imu.getAngleZ();
#endif
#if FT_ENABLED(USE_HMC5883)
    data.heading = _mag.getHeading();
#elif FT_ENABLED(USE_MPU6050)
    data.heading = _imu.getAngleZ();
#endif
#if FT_ENABLED(USE_BMP180)
    data.altitude = _bmp.getAltitude();
    data.bmp_temp = _bmp.getTemperature();
    data.pressure = _bmp.getPressure();
#endif
}

void Peripherals::getSettingsProto(socket_message_PeripheralSettingsData &data) {
    data.sda = state().sda;
    data.scl = state().scl;
    data.frequency = state().frequency;
    data.pins_count = 0;
}

/* IMU FUNCTIONS */
bool Peripherals::readImu() {
    bool updated = false;
#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
    beginTransaction();
    updated = _imu.update();
    endTransaction();
#endif
    return updated;
}

bool Peripherals::readMag() {
    bool updated = false;
#if FT_ENABLED(USE_HMC5883)
    beginTransaction();
    updated = _mag.update();
    endTransaction();
#endif
    return updated;
}

bool Peripherals::readBMP() {
    bool updated = false;
#if FT_ENABLED(USE_BMP180)
    beginTransaction();
    updated = _bmp.update();
    endTransaction();
#endif
    return updated;
}

bool Peripherals::readGesture() {
    bool updated = false;
#if FT_ENABLED(USE_PAJ7620U2)
    beginTransaction();
    updated = _gesture.readGesture();
    endTransaction();
#endif
    return updated;
}

void Peripherals::readSonar() {
#if FT_ENABLED(USE_USS)
    _left_distance = _left_sonar->ping_cm();
    vTaskDelay(50 / portTICK_PERIOD_MS);
    _right_distance = _right_sonar->ping_cm();
#endif
}

float Peripherals::angleX() {
    return
#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
        _imu.getAngleX();
#else
        0;
#endif
}

float Peripherals::angleY() {
    return
#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
        _imu.getAngleY();
#else
        0;
#endif
}

float Peripherals::angleZ() {
    return
#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
        _imu.getAngleZ();
#else
        0;
#endif
}

gesture_t Peripherals::takeGesture() {
    return
#if FT_ENABLED(USE_PAJ7620U2)
        _gesture.takeGesture();
#else
        gesture_t::eGestureNone;
#endif
}

float Peripherals::leftDistance() { return _left_distance; }
float Peripherals::rightDistance() { return _right_distance; }

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