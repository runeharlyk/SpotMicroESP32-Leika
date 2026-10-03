// Host test of the stored robot settings and their variant, built and run by test_host_programs.py.
#include <cstdio>
#include <cstring>
#include <robot_service.h>
#include <pb_decode.h>

namespace FileSystem {
// The settings file cannot be written on the host; the service keeps working from memory.
bool mkdirRecursive(const char *) { return false; }
} // namespace FileSystem

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static RobotSettings decoded(const uint8_t *bytes, size_t size) {
    RobotSettings proto = socket_message_RobotSettings_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(bytes, size);
    CHECK(pb_decode(&stream, socket_message_RobotSettings_fields, &proto));
    return proto;
}

// What the firmware before the variant stored: the name alone. Such a robot keeps its name and has no variant.
static void aFileFromBeforeTheVariantLoadsWithoutOne() {
    const uint8_t file[] = {0x0A, 5, 'P', 'i', 'c', 'o', '1'};
    RobotSettings settings = RobotSettings_defaults();
    settings.variant = socket_message_KinematicsVariant_SPOTMICRO_ESP32;
    CHECK(RobotSettings_update(decoded(file, sizeof(file)), settings) == StateUpdateResult::CHANGED);
    CHECK(std::strcmp(settings.name, "Pico1") == 0);
    CHECK(settings.variant == socket_message_KinematicsVariant_KINEMATICS_VARIANT_UNSET);
}

// A newer firmware's variant must not make the file unloadable, which would reset the name with it.
static void anUnknownStoredVariantCountsAsNone() {
    const uint8_t file[] = {0x0A, 5, 'P', 'i', 'c', 'o', '1', 0x10, 9};
    RobotSettings settings = RobotSettings_defaults();
    CHECK(RobotSettings_update(decoded(file, sizeof(file)), settings) == StateUpdateResult::CHANGED);
    CHECK(std::strcmp(settings.name, "Pico1") == 0);
    CHECK(settings.variant == socket_message_KinematicsVariant_KINEMATICS_VARIANT_UNSET);
}

static void renamingKeepsTheVariantAndChoosingKeepsTheName() {
    RobotService robot;
    robot.begin();  // no file on the host: the factory name
    CHECK(robot.chooseVariant(socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI));
    CHECK(robot.rename("Pico Two"));
    CHECK(robot.variant() == socket_message_KinematicsVariant_SPOTMICRO_ESP32_MINI);
    CHECK(robot.chooseVariant(socket_message_KinematicsVariant_SPOTMICRO_YERTLE));
    CHECK(robot.name() == "Pico Two");
    CHECK(robot.variant() == socket_message_KinematicsVariant_SPOTMICRO_YERTLE);
}

static void onlyAVariantTheFirmwareDrivesCanBeChosen() {
    RobotService robot;
    robot.begin();  // no file on the host: the factory name
    CHECK(robot.chooseVariant(socket_message_KinematicsVariant_SPOTMICRO_ESP32));
    CHECK(!robot.chooseVariant(socket_message_KinematicsVariant_KINEMATICS_VARIANT_UNSET));
    CHECK(!robot.chooseVariant(static_cast<KinematicsVariant>(9)));
    CHECK(robot.variant() == socket_message_KinematicsVariant_SPOTMICRO_ESP32);
}

int main() {
    aFileFromBeforeTheVariantLoadsWithoutOne();
    anUnknownStoredVariantCountsAsNone();
    renamingKeepsTheVariantAndChoosingKeepsTheName();
    onlyAVariantTheFirmwareDrivesCanBeChosen();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
