#pragma once

#include <communication/http_server.h>
#include <platform_shared/message.pb.h>
#include <filesystem.h>

#define KINEMATICS_VARIANT_STR "default"

#ifndef USE_CAMERA
#define USE_CAMERA 0
#endif
#ifndef USE_MOTION
#define USE_MOTION 1
#endif
#ifndef USE_BNO055
#define USE_BNO055 0
#endif
#ifndef USE_MPU6050
#define USE_MPU6050 1
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
#ifndef EMBED_WEBAPP
#define EMBED_WEBAPP 1
#endif
#ifndef USE_PAJ7620U2
#define USE_PAJ7620U2 0
#endif

#define FT_ENABLED(feature) (feature)

namespace feature_service {

void printFeatureConfiguration();
void features_request(const socket_message_FeaturesDataRequest& fd_req, socket_message_FeaturesDataResponse& fd_res);
void features(socket_message_FeaturesDataResponse& proto);
esp_err_t getFeatures(HttpRequest& request);

} // namespace feature_service
