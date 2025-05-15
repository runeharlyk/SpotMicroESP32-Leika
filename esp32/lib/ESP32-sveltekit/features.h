#ifndef Features_h
#define Features_h

#include <WiFi.h>
#include <ArduinoJson.h>
#include <PsychicHttp.h>

#define FT_ENABLED(feature) feature

// upload firmware feature off by default
#ifndef USE_UPLOAD_FIRMWARE
#define USE_UPLOAD_FIRMWARE 0
#endif

// download firmware feature off by default
#ifndef USE_DOWNLOAD_FIRMWARE
#define USE_DOWNLOAD_FIRMWARE 0
#endif

// ESP32 sleep states off by default
#ifndef USE_SLEEP
#define USE_SLEEP 0
#endif

// ESP32 camera off by default
#ifndef USE_CAMERA
#define USE_CAMERA 0
#endif

// ESP32 IMU on by default
#ifndef USE_MPU6050
#define USE_MPU6050 0
#endif

// ESP32 IMU on by default
#ifndef USE_BNO055
#define USE_BNO055 1
#endif

// ESP32 magnetometer on by default
#ifndef USE_HMC5883
#define USE_HMC5883 0
#endif

// ESP32 barometer off by default
#ifndef USE_BMP180
#define USE_BMP180 0
#endif

// ESP32 SONAR off by default
#ifndef USE_USS
#define USE_USS 0
#endif

// PCA9685 Servo controller on by default
#ifndef USE_PCA9685
#define USE_PCA9685 1
#endif

// ESP32 GPS off by default
#ifndef USE_GPS
#define USE_GPS 0
#endif

// ESP32 MDNS on by default
#ifndef USE_MDNS
#define USE_MDNS 1
#endif

namespace feature_service {

void printFeatureConfiguration();

void features(JsonObject &root);

esp_err_t getFeatures(PsychicRequest *request);

} // namespace feature_service

#endif
