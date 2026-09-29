#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// Free of ESP-IDF headers so the text rules can be checked on a host; settings/placeholders.h
// supplies the chip-specific values.

inline std::string formatDeviceId(const uint8_t (&mac)[6]) {
    char id[13];
    snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return id;
}

// Expands the factory-settings placeholders: "#{platform}" and "#{unique_id}".
inline std::string expandPlaceholders(const char *tmpl, const std::string &platform, const std::string &uniqueId) {
    std::string out(tmpl);
    auto replaceAll = [&out](const std::string &token, const std::string &value) {
        for (size_t pos = out.find(token); pos != std::string::npos; pos = out.find(token, pos + value.size())) {
            out.replace(pos, token.size(), value);
        }
    };
    replaceAll("#{platform}", platform);
    replaceAll("#{unique_id}", uniqueId);
    return out;
}

// RFC 1123 host label: letters, digits and inner hyphens, at most 63 characters. Anything else
// becomes a hyphen so DHCP and mDNS accept the name.
inline std::string toHostLabel(const std::string &text) {
    std::string label;
    for (char c : text) {
        label += std::isalnum(static_cast<unsigned char>(c)) ? c : '-';
    }
    const size_t first = label.find_first_not_of('-');
    if (first == std::string::npos) return "";
    label = label.substr(first, 63);
    return label.substr(0, label.find_last_not_of('-') + 1);
}

constexpr size_t ROBOT_NAME_MAX_BYTES = 32;

// A robot name is 1 to 32 bytes of UTF-8 once surrounding spaces are trimmed, with no control characters.
inline bool normalizeRobotName(const char *input, std::string &name) {
    std::string candidate(input);
    const size_t first = candidate.find_first_not_of(' ');
    if (first == std::string::npos) return false;
    candidate = candidate.substr(first, candidate.find_last_not_of(' ') - first + 1);
    if (candidate.size() > ROBOT_NAME_MAX_BYTES) return false;
    for (unsigned char c : candidate) {
        if (c < 0x20 || c == 0x7F) return false;
    }
    name = candidate;
    return true;
}
