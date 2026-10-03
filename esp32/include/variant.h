#pragma once

#include <joint_model.h>
#include <kinematics.h>
#include <platform_shared/message.pb.h>

using KinematicsVariant = socket_message_KinematicsVariant;

/** Whether `variant` is one of the variants this firmware drives, rather than none or one it does not know. */
constexpr bool knownVariant(KinematicsVariant variant) {
    return variant == socket_message_KinematicsVariant_SPOTMICRO_ESP32 ||
           variant == socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI ||
           variant == socket_message_KinematicsVariant_SPOTMICRO_YERTLE;
}

/** The variant's leg geometry; null while no variant is chosen. */
constexpr const KinConfig *kinConfigFor(KinematicsVariant variant) {
    switch (variant) {
        case socket_message_KinematicsVariant_SPOTMICRO_ESP32: return &KIN_CONFIG_SPOTMICRO_ESP32;
        case socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI: return &KIN_CONFIG_SPOTMICRO_ESP32_MINI;
        case socket_message_KinematicsVariant_SPOTMICRO_YERTLE: return &KIN_CONFIG_SPOTMICRO_YERTLE;
        default: return nullptr;
    }
}

/** How the variant's servos sit; null while no variant is chosen. */
constexpr const JointModel *jointModelFor(KinematicsVariant variant) {
    switch (variant) {
        case socket_message_KinematicsVariant_SPOTMICRO_ESP32: return &JOINT_MODEL_SPOTMICRO_ESP32;
        case socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI: return &JOINT_MODEL_SPOTMICRO_ESP32_MINI;
        case socket_message_KinematicsVariant_SPOTMICRO_YERTLE: return &JOINT_MODEL_SPOTMICRO_YERTLE;
        default: return nullptr;
    }
}

/** The name the app, the recorder and the simulation know the variant by; empty while none is chosen. */
constexpr const char *variantName(KinematicsVariant variant) {
    switch (variant) {
        case socket_message_KinematicsVariant_SPOTMICRO_ESP32: return "SPOTMICRO_ESP32";
        case socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI: return "SPOTMICRO_ESP32_MINI";
        case socket_message_KinematicsVariant_SPOTMICRO_YERTLE: return "SPOTMICRO_YERTLE";
        default: return "";
    }
}
