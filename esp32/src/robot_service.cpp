#include <robot_service.h>

RobotService::RobotService()
    : _persistence(RobotSettings_read, RobotSettings_update, this, ROBOT_SETTINGS_FILE, socket_message_RobotSettings_fields,
                   socket_message_RobotSettings_size, RobotSettings_defaults()) {}

void RobotService::begin() { _persistence.readFromFS(); }

bool RobotService::rename(const char *name) {
    return update(
               [&](RobotSettings &settings) {
                   RobotSettings requested = settings;
                   strncpy(requested.name, name, sizeof(requested.name) - 1);
                   requested.name[sizeof(requested.name) - 1] = '\0';
                   return RobotSettings_update(requested, settings);
               },
               "socket") != StateUpdateResult::ERROR;
}

bool RobotService::chooseVariant(KinematicsVariant variant) {
    if (!knownVariant(variant)) return false;
    return update(
               [&](RobotSettings &settings) {
                   RobotSettings requested = settings;
                   requested.variant = variant;
                   return RobotSettings_update(requested, settings);
               },
               "socket") != StateUpdateResult::ERROR;
}
