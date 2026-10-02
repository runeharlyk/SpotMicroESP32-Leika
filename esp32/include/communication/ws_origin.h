#pragma once

#include <cstdint>
#include <initializer_list>
#include <string_view>

// Where the web app is hosted (deploy.yml): a fork that deploys its own sets it in factory_settings.ini.
#ifndef HOSTED_APP_ORIGIN
#define HOSTED_APP_ORIGIN "https://runeharlyk.github.io"
#endif

namespace ws_origin_detail {

inline bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        char x = a[i] >= 'A' && a[i] <= 'Z' ? a[i] - 'A' + 'a' : a[i];
        char y = b[i] >= 'A' && b[i] <= 'Z' ? b[i] - 'A' + 'a' : b[i];
        if (x != y) return false;
    }
    return true;
}

inline bool isDigits(std::string_view text) {
    if (text.empty()) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// "host", "host:port" or "[::1]:port" to the host; empty when the port is not a number.
inline std::string_view hostOf(std::string_view authority) {
    size_t colon = authority.rfind(':');
    bool bracketed = !authority.empty() && authority[0] == '[';
    size_t closing = authority.find(']');
    if (colon == std::string_view::npos || (bracketed && colon < closing)) return authority;
    return isDigits(authority.substr(colon + 1)) ? authority.substr(0, colon) : std::string_view();
}

inline bool parseIPv4(std::string_view host, uint8_t (&octets)[4]) {
    for (int i = 0; i < 4; i++) {
        size_t dot = i < 3 ? host.find('.') : host.size();
        if (dot == std::string_view::npos) return false;
        std::string_view part = host.substr(0, dot);
        if (!isDigits(part) || part.size() > 3) return false;
        int value = 0;
        for (char c : part) value = value * 10 + (c - '0');
        if (value > 255) return false;
        octets[i] = static_cast<uint8_t>(value);
        host = i < 3 ? host.substr(dot + 1) : std::string_view();
    }
    return true;
}

// Loopback, the private ranges, link-local, and mDNS names: hosts only the local network can reach.
inline bool isLocal(std::string_view host) {
    if (equalsIgnoringCase(host, "localhost") || host == "[::1]") return true;
    constexpr std::string_view mdns = ".local";
    if (host.size() > mdns.size() && equalsIgnoringCase(host.substr(host.size() - mdns.size()), mdns)) return true;
    uint8_t ip[4];
    if (!parseIPv4(host, ip)) return false;
    return ip[0] == 127 || ip[0] == 10 || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) ||
           (ip[0] == 192 && ip[1] == 168) || (ip[0] == 169 && ip[1] == 254);
}

} // namespace ws_origin_detail

/**
 * Whether a WebSocket upgrade may proceed. Browsers name the page that opens a socket in its Origin, and
 * without this check any page from the internet, open in a browser on the network, could drive the
 * robot and read its settings. Allowed are pages served from the local network (the robot's own app,
 * the fleet's other robots, a development server), the hosted app, which Chrome lets reach local
 * addresses once the user allows local network access, and clients that are not browsers, which send
 * no Origin at all.
 */
inline bool wsOriginAllowed(const char *origin, const char *host) {
    using namespace ws_origin_detail;
    if (origin == nullptr) return true;
    std::string_view page(origin);
    if (equalsIgnoringCase(page, HOSTED_APP_ORIGIN)) return true;
    std::string_view authority;
    for (std::string_view scheme : {"http://", "https://"}) {
        if (page.substr(0, scheme.size()) == scheme) authority = page.substr(scheme.size());
    }
    std::string_view pageHost = hostOf(authority);
    if (pageHost.empty()) return false;
    if (isLocal(pageHost)) return true;
    return host != nullptr && equalsIgnoringCase(authority, host);
}
