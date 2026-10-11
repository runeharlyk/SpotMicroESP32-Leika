// Host test of fs_paths.h, built and run by test_host_programs.py.
#include <cstdio>
#include <fs_paths.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// What the app's file browser asks for: paths it builds from the root and the entries a listing returned.
static void pathsTheFileBrowserBuildsAreAllowed() {
    CHECK(fsPathAllowed("/"));
    CHECK(fsPathAllowed("/notes.txt"));
    CHECK(fsPathAllowed("/media"));
    CHECK(fsPathAllowed("/media/"));
    CHECK(fsPathAllowed("/media/cat.gif"));
    CHECK(fsPathAllowed("/media/.hidden"));
    CHECK(fsPathAllowed("/media/..."));
}

// Names that only resemble the settings directory are other entries.
static void namesThatOnlyResembleConfigAreAllowed() {
    CHECK(fsPathAllowed("/configuration/notes.txt"));
    CHECK(fsPathAllowed("/config.txt"));
    CHECK(fsPathAllowed("/media/config"));
    CHECK(fsPathAllowed("/media/config/wifiSettings.pb"));
}

static void theSettingsDirectoryIsRefused() {
    CHECK(!fsPathAllowed("/config"));
    CHECK(!fsPathAllowed("/config/"));
    CHECK(!fsPathAllowed("/config/wifiSettings.pb"));
    CHECK(!fsPathAllowed("/config/apSettings.pb"));
    CHECK(!fsPathAllowed("/config/wifiSettings.pb.part"));
    CHECK(!fsPathAllowed("/config/new"));
}

// Every way of spelling a path into /config without starting with it.
static void detoursIntoTheSettingsDirectoryAreRefused() {
    CHECK(!fsPathAllowed("//config/wifiSettings.pb"));
    CHECK(!fsPathAllowed("/media//config"));
    CHECK(!fsPathAllowed("/./config/apSettings.pb"));
    CHECK(!fsPathAllowed("/media/../config/wifiSettings.pb"));
    CHECK(!fsPathAllowed("/media/.."));
    CHECK(!fsPathAllowed("/.."));
    CHECK(!fsPathAllowed("/../littlefs/config"));
    CHECK(!fsPathAllowed("/."));
}

static void relativePathsAreRefused() {
    CHECK(!fsPathAllowed(""));
    CHECK(!fsPathAllowed("media/cat.gif"));
    CHECK(!fsPathAllowed("config/wifiSettings.pb"));
    CHECK(!fsPathAllowed("../config"));
}

// Deleting the root would take /config with it, and uploading to it names no file.
static void onlyAListingMayNameTheRoot() {
    CHECK(!fsEntryAllowed("/"));
    CHECK(fsEntryAllowed("/media"));
    CHECK(fsEntryAllowed("/notes.txt"));
    CHECK(!fsEntryAllowed("/config"));
    CHECK(!fsEntryAllowed("//"));
}

int main() {
    pathsTheFileBrowserBuildsAreAllowed();
    namesThatOnlyResembleConfigAreAllowed();
    theSettingsDirectoryIsRefused();
    detoursIntoTheSettingsDirectoryAreRefused();
    relativePathsAreRefused();
    onlyAListingMayNameTheRoot();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
