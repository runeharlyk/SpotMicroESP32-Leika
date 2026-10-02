#include <robot_service.h>

RobotService::RobotService()
    : _persistence(RobotSettings_read, RobotSettings_update, this, ROBOT_SETTINGS_FILE, socket_message_RobotSettings_fields,
                   socket_message_RobotSettings_size, RobotSettings_defaults()) {}

void RobotService::begin() { _persistence.readFromFS(); }

bool RobotService::rename(const char *name) {
    RobotSettings requested = socket_message_RobotSettings_init_zero;
    strncpy(requested.name, name, sizeof(requested.name) - 1);
    return update([&](RobotSettings &settings) { return RobotSettings_update(requested, settings); }, "socket") !=
           StateUpdateResult::ERROR;
}
