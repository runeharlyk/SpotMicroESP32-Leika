#pragma once

#include <template/state_result.h>
#include <platform_shared/message.pb.h>
#include <settings/placeholders.h>
#include <variant.h>
#include <cstring>

#ifndef FACTORY_ROBOT_NAME
#define FACTORY_ROBOT_NAME "Spot Micro #{unique_id}"
#endif

using RobotSettings = socket_message_RobotSettings;

inline RobotSettings RobotSettings_defaults() {
    RobotSettings settings = socket_message_RobotSettings_init_zero;
    strncpy(settings.name, substitutePlaceholders(FACTORY_ROBOT_NAME).c_str(), sizeof(settings.name) - 1);
    return settings;
}

inline void RobotSettings_read(const RobotSettings &settings, RobotSettings &proto) { proto = settings; }

// A variant this firmware does not know, stored by a newer one, counts as none chosen, so the name survives a
// downgrade.
inline StateUpdateResult RobotSettings_update(const RobotSettings &proto, RobotSettings &settings) {
    std::string name;
    if (!normalizeRobotName(proto.name, name)) return StateUpdateResult::ERROR;
    const KinematicsVariant variant =
        knownVariant(proto.variant) ? proto.variant : socket_message_KinematicsVariant_KINEMATICS_VARIANT_UNSET;
    if (name == settings.name && variant == settings.variant) return StateUpdateResult::UNCHANGED;
    strncpy(settings.name, name.c_str(), sizeof(settings.name) - 1);
    settings.name[sizeof(settings.name) - 1] = '\0';
    settings.variant = variant;
    return StateUpdateResult::CHANGED;
}
