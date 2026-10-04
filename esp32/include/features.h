#pragma once

#include <peripherals/peripherals.h>
#include <variant.h>
#include "platform_shared/message.pb.h"

// The camera is the board's, not the robot's: its pins and driver come with the build env.
#ifndef USE_CAMERA
#define USE_CAMERA 0
#endif

namespace feature_service {

/** What this boot runs with, as the robot reports it to the app. */
struct RuntimeFeatures {
    const char *robotName;
    const char *hostname;
    KinematicsVariant variant;
    SensorStatus sensors;
    bool servoDetected;
    bool cameraDetected;
    bool cameraActive;
    bool ws2812;
};

/** The PlatformIO env this firmware was built for. */
const char *buildTarget();

void printFeatureConfiguration();

void features_request(const RuntimeFeatures &features, socket_message_FeaturesDataResponse &fd_res);

} // namespace feature_service
