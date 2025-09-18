#include <peripherals/peripherals.h>

Peripherals::Peripherals()
    : endpoint(PeripheralsConfiguration::read, PeripheralsConfiguration::update, this),
      _eventEndpoint(PeripheralsConfiguration::read, PeripheralsConfiguration::update, this,
                     EVENT_CONFIGURATION_SETTINGS),
      _persistence(PeripheralsConfiguration::read, PeripheralsConfiguration::update, this, DEVICE_CONFIG_FILE) {
    _accessMutex = xSemaphoreCreateMutex();
    addUpdateHandler([&](const std::string &originId) { updatePins(); }, false);
}

void Peripherals::begin() {
    _eventEndpoint.begin();
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
    readImu();
    readMag();
    EXECUTE_EVERY_N_MS(100, { readGesture(); });
    EXECUTE_EVERY_N_MS(500, { readBMP(); });
    EXECUTE_EVERY_N_MS(500, { readSonar(); });
}

void Peripherals::updatePins() {
    if (i2c_active) {
        Wire.end();
    }

    if (state().sda != -1 && state().scl != -1) {
        Wire.begin(state().sda, state().scl, state().frequency);
        i2c_active = true;
    }
}

void Peripherals::getI2CResult(JsonVariant &root) {
    char output[150];
    root["sda"] = state().sda;
    root["scl"] = state().scl;
    JsonArray addresses = root["addresses"].to<JsonArray>();
    for (auto &address : addressList) {
        addresses.add(address);
    }
    ESP_LOGI("Peripherals", "Emitting I2C scan results: %d", addressList.size());
}

void Peripherals::scanI2C(uint8_t lower, uint8_t higher) {
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
    updated = _bmp.readBarometer();
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

void Peripherals::getIMUResult(JsonVariant &root) {
#if FT_ENABLED(USE_MPU6050 || USE_BNO055)
    _imu.getResults(root);
#endif
#if FT_ENABLED(USE_HMC5883)
    _mag.getResults(root);
#endif
#if FT_ENABLED(USE_BMP180)
    _bmp.getResults(root);
#endif
}

void Peripherals::getSonarResult(JsonVariant &root) {
#if FT_ENABLED(USE_USS)
    JsonArray array = root.to<JsonArray>();
    array[0] = _left_distance;
    array[1] = _right_distance;
#endif
}