// Host test of template/stateful_persistence.h and template/stateful_proto_handler.h on the host's file
// system, built with nanopb and run in a scratch directory by test_host_programs.py.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <template/stateful_persistence.h>
#include <template/stateful_proto_handler.h>

namespace FileSystem {
bool mkdirRecursive(const char *path) { return std::filesystem::create_directories(path); }
} // namespace FileSystem

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

using Settings = api_PeripheralSettings;
static const char *FILE_PATH = "config/peripheralSettings.pb";
static const std::string BACKUP_PATH = std::string(FILE_PATH) + ".bak";

// As the settings headers do: the state is the proto, and an update may refuse what it is given.
static void readSettings(const Settings &settings, Settings &proto) { proto = settings; }
static StateUpdateResult updateSettings(const Settings &proto, Settings &settings) {
    if (proto.frequency <= 0) return StateUpdateResult::ERROR;
    settings = proto;
    return StateUpdateResult::CHANGED;
}

static Settings defaults() {
    Settings settings = api_PeripheralSettings_init_zero;
    settings.sda = 21;
    settings.scl = 22;
    settings.frequency = 400000;
    return settings;
}

static Settings withPins(int sda, int scl, int frequency = 100000) {
    Settings settings = api_PeripheralSettings_init_zero;
    settings.sda = sda;
    settings.scl = scl;
    settings.frequency = frequency;
    return settings;
}

struct Robot {
    StatefulService<Settings> service;
    FSPersistencePB<Settings> persistence {readSettings, updateSettings, &service, FILE_PATH,
                                           api_PeripheralSettings_fields, api_PeripheralSettings_size, defaults()};
    StatefulProtoHandler<Settings, Settings> protoHandler {readSettings, updateSettings, &service,
                                                           api_PeripheralSettings_fields};
    int reconfigurations = 0;

    Robot() {
        service.addUpdateHandler([this](const std::string &) { reconfigurations++; });
    }
    Settings settings() {
        Settings settings;
        service.read([&](const Settings &state) { settings = state; });
        return settings;
    }
};

static std::vector<char> contents(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static void write(const std::string &path, const std::vector<char> &bytes) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream(path, std::ios::binary).write(bytes.data(), bytes.size());
}

static void fresh() { std::filesystem::remove_all("config"); }

static void savedSettingsSurviveARestart() {
    fresh();
    {
        Robot robot;
        robot.persistence.readFromFS();
        CHECK(robot.protoHandler.update(withPins(5, 6)) == StateUpdateResult::CHANGED);
    }
    Robot restarted;
    restarted.persistence.readFromFS();
    CHECK(restarted.settings().sda == 5);
    CHECK(restarted.settings().scl == 6);
    CHECK(!std::filesystem::exists(BACKUP_PATH));
}

static void aFirstBootStoresTheDefaults() {
    fresh();
    Robot robot;
    robot.persistence.readFromFS();
    CHECK(robot.settings().sda == 21);
    CHECK(std::filesystem::exists(FILE_PATH));
    CHECK(!std::filesystem::exists(BACKUP_PATH));
}

// Field 1 (sda, a varint) stored as length-delimited: what a firmware that changed the field's type wrote.
static void anUnreadableFileIsKeptAsABackupBeforeTheDefaultsReplaceIt() {
    fresh();
    const std::vector<char> unreadable {0x0A, 0x02, 0x01, 0x02};
    write(FILE_PATH, unreadable);
    Robot robot;
    robot.persistence.readFromFS();
    CHECK(robot.settings().sda == 21);
    CHECK(contents(BACKUP_PATH) == unreadable);
    Robot restarted;
    restarted.persistence.readFromFS();
    CHECK(restarted.settings().sda == 21);
}

static void storedSettingsTheUpdateRefusesAreKeptAsABackupToo() {
    fresh();
    {
        Robot robot;
        robot.persistence.readFromFS();
        robot.persistence.disableUpdateHandler();
        robot.service.updateWithoutPropagation([](Settings &state) {
            state = withPins(5, 6, 0);
            return StateUpdateResult::CHANGED;
        });
        CHECK(robot.persistence.writeToFS());
    }
    const std::vector<char> refused = contents(FILE_PATH);
    Robot robot;
    robot.persistence.readFromFS();
    CHECK(robot.settings().sda == 21);
    CHECK(robot.settings().frequency == 400000);
    CHECK(contents(BACKUP_PATH) == refused);
}

// The app saves the whole form: pressing save without an edit must not restart the bus or rewrite flash.
static void aSaveThatChangesNothingNeitherReconfiguresNorWrites() {
    fresh();
    Robot robot;
    robot.persistence.readFromFS();
    CHECK(robot.protoHandler.update(withPins(5, 6)) == StateUpdateResult::CHANGED);
    CHECK(robot.reconfigurations == 1);
    std::filesystem::remove(FILE_PATH);
    CHECK(robot.protoHandler.update(withPins(5, 6)) == StateUpdateResult::UNCHANGED);
    CHECK(robot.reconfigurations == 1);
    CHECK(!std::filesystem::exists(FILE_PATH));
    CHECK(robot.protoHandler.update(withPins(5, 7)) == StateUpdateResult::CHANGED);
    CHECK(robot.reconfigurations == 2);
    CHECK(std::filesystem::exists(FILE_PATH));
}

static void aRefusedSaveStillReportsTheRefusal() {
    fresh();
    Robot robot;
    robot.persistence.readFromFS();
    CHECK(robot.protoHandler.update(withPins(5, 6, 0)) == StateUpdateResult::ERROR);
    CHECK(robot.reconfigurations == 0);
    CHECK(robot.settings().sda == 21);
}

int main() {
    savedSettingsSurviveARestart();
    aFirstBootStoresTheDefaults();
    anUnreadableFileIsKeptAsABackupBeforeTheDefaultsReplaceIt();
    storedSettingsTheUpdateRefusesAreKeptAsABackupToo();
    aSaveThatChangesNothingNeitherReconfiguresNorWrites();
    aRefusedSaveStillReportsTheRefusal();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
