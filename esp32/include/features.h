#ifndef Features_h
#define Features_h

#include <WiFi.h>
#include <ArduinoJson.h>
#include <PsychicHttp.h>

#define FT_ENABLED(feature) feature

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

// WS2812 LED strip off by default
#ifndef USE_WS2812
#define USE_WS2812 0
#endif

// ESP32 MDNS on by default
#ifndef USE_MDNS
#define USE_MDNS 1
#endif

// ESP32 MSGPACK on by default
#ifndef USE_MSGPACK
#define USE_MSGPACK 1
#endif

// ESP32 JSON off by default
#ifndef USE_JSON
#define USE_JSON 0
#endif

static_assert(!(USE_JSON == 1 && USE_MSGPACK == 1), "Cannot set both USE_JSON and USE_MSGPACK to 1 simultaneously");

#if defined(SPOTMICRO_ESP32) && defined(SPOTMICRO_ESP32_MINI) && defined(SPOTMICRO_YERTLE)
#error "Only one kinematics variant must be defined"
#endif

#if !defined(SPOTMICRO_ESP32) && !defined(SPOTMICRO_ESP32_MINI) && !defined(SPOTMICRO_YERTLE)
#error "You must define one kinematics variant"
#endif

#if defined(SPOTMICRO_ESP32)
#define KINEMATICS_VARIANT_STR "SPOTMICRO_ESP32"
#elif defined(SPOTMICRO_ESP32_MINI)
#define KINEMATICS_VARIANT_STR "SPOTMICRO_ESP32_MINI"
#elif defined(SPOTMICRO_YERTLE)
#define KINEMATICS_VARIANT_STR "SPOTMICRO_YERTLE"
#else
#define KINEMATICS_VARIANT_STR "UNKNOWN"
#endif

namespace feature_service {

void printFeatureConfiguration();

void features(JsonObject &root);

esp_err_t getFeatures(PsychicRequest *request);

} // namespace feature_service

#endif
