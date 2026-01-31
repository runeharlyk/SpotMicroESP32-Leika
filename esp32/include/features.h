#pragma once

#include <wifi/wifi_idf.h>
#include <ArduinoJson.h>
#include <esp_http_server.h>
#include "platform_shared/message.pb.h"

#define FT_ENABLED(feature) feature

#ifndef USE_CAMERA
#define USE_CAMERA 0
#endif

#ifndef USE_MPU6050
#define USE_MPU6050 0
#endif

#ifndef USE_BNO055
#define USE_BNO055 1
#endif

#ifndef USE_HMC5883
#define USE_HMC5883 0
#endif

#ifndef USE_BMP180
#define USE_BMP180 0
#endif

#ifndef USE_USS
#define USE_USS 0
#endif

#ifndef USE_PCA9685
#define USE_PCA9685 1
#endif

#ifndef USE_WS2812
#define USE_WS2812 0
#endif

#ifndef USE_MDNS
#define USE_MDNS 1
#endif

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

void features_request(const socket_message_FeaturesDataRequest& fd_req, socket_message_FeaturesDataResponse& fd_res);

} // namespace feature_service
