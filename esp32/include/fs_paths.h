#pragma once

#include <string_view>

/**
 * Whether the file API may touch a path below the mount point. The settings in /config hold the WiFi and AP
 * passwords, which the settings messages mask (settings/app_network_settings.h): reading the files would undo
 * that, and writing them would bypass the settings' validation. Paths must be absolute, without "." or ".."
 * segments or empty ones between slashes; a trailing slash is allowed.
 */
inline bool fsPathAllowed(std::string_view path) {
    if (path.empty() || path[0] != '/') return false;
    size_t start = 1;
    bool first = true;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string_view::npos) end = path.size();
        const std::string_view segment = path.substr(start, end - start);
        if (segment == "." || segment == "..") return false;
        if (segment.empty() && end != path.size()) return false;
        if (first && segment == "config") return false;
        first = false;
        start = end + 1;
    }
    return true;
}

/** An allowed path that names an entry, not the root: deleting the root would take /config with it. */
inline bool fsEntryAllowed(std::string_view path) { return fsPathAllowed(path) && path != "/"; }
