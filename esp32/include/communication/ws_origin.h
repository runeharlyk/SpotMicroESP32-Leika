#pragma once

#include <string_view>

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

// The developer's machine, at any port: "localhost", "localhost:5173", "127.0.0.1:4173", "[::1]:5173".
inline bool isLoopback(std::string_view authority) {
    for (std::string_view name : {"localhost", "127.0.0.1", "[::1]"}) {
        if (authority.substr(0, name.size()) != name) continue;
        std::string_view port = authority.substr(name.size());
        if (port.empty()) return true;
        if (port[0] != ':' || port.size() == 1) return false;
        for (char c : port.substr(1)) {
            if (c < '0' || c > '9') return false;
        }
        return true;
    }
    return false;
}

} // namespace ws_origin_detail

/**
 * Whether a WebSocket upgrade may proceed. Browsers name the page that opens a socket in its Origin, and
 * without this check any page open on the LAN could drive the robot and read its settings. Allowed are
 * the app the robot serves itself (the Origin names the Host the request went to), the app served from
 * the developer's own machine, and clients that are not browsers, which send no Origin at all.
 */
inline bool wsOriginAllowed(const char *origin, const char *host) {
    using namespace ws_origin_detail;
    if (origin == nullptr) return true;
    constexpr std::string_view scheme = "http://";
    std::string_view page(origin);
    if (page.substr(0, scheme.size()) != scheme) return false;
    std::string_view authority = page.substr(scheme.size());
    if (isLoopback(authority)) return true;
    return host != nullptr && !authority.empty() && equalsIgnoringCase(authority, host);
}
