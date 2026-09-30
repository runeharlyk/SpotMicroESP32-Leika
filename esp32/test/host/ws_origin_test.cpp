// Host test of communication/ws_origin.h, built and run by test_host_programs.py.
#include <cstdio>
#include <communication/ws_origin.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// The embedded app, however the robot was reached: its IP, its mDNS name, or its access point.
static void theAppTheRobotServesMayConnect() {
    CHECK(wsOriginAllowed("http://192.168.1.39", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://spot-micro-27cb70.local", "spot-micro-27cb70.local"));
    CHECK(wsOriginAllowed("http://Spot-Micro-27CB70.local", "spot-micro-27cb70.local"));
    CHECK(wsOriginAllowed("http://192.168.4.1:80", "192.168.4.1:80"));
}

// pnpm dev and vite preview serve the app from the developer's own machine.
static void theAppServedLocallyForDevelopmentMayConnect() {
    CHECK(wsOriginAllowed("http://localhost:5173", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://127.0.0.1:4173", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://[::1]:5173", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://localhost", "192.168.1.39"));
    CHECK(wsOriginAllowed("https://localhost:5173", "192.168.1.39"));
}

// The app another robot serves scans for and drives the rest of the fleet, and vite dev --host serves
// the app to a phone on the same network: pages on the local network may connect.
static void theAppServedFromTheLocalNetworkMayConnect() {
    CHECK(wsOriginAllowed("http://192.168.1.40", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://192.168.1.23:5173", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://spot-micro-aabbcc.local", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://10.0.0.7", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://172.16.0.1", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://172.31.255.254:8080", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://169.254.12.1", "192.168.1.39"));
    CHECK(wsOriginAllowed("http://192.168.4.2", "192.168.4.1"));
}

// The hosted app: Chrome lets an HTTPS page reach a local address once the user allows local network
// access, so it scans for and drives robots from GitHub Pages.
static void theHostedAppMayConnect() {
    CHECK(wsOriginAllowed("https://runeharlyk.github.io", "192.168.1.39"));
    CHECK(wsOriginAllowed("https://RuneHarlyk.github.io", "spot-micro-27cb70.local"));
}

// Scripts and tools are not browsers and send no Origin; a page cannot omit it.
static void aClientWithoutAnOriginMayConnect() { CHECK(wsOriginAllowed(nullptr, "192.168.1.39")); }

// A page from the internet, open in any browser on the network, could otherwise drive the robot.
static void anyOtherPageMayNot() {
    CHECK(!wsOriginAllowed("http://evil.example", "192.168.1.39"));
    CHECK(!wsOriginAllowed("https://runeharlyk.github.io.evil.example", "192.168.1.39"));
    CHECK(!wsOriginAllowed("https://evil.github.io", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://runeharlyk.github.io", "192.168.1.39"));
    CHECK(!wsOriginAllowed("null", "192.168.1.39"));
    CHECK(!wsOriginAllowed("", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://192.168.1.39.evil.example", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://localhost.evil.example", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://localhost:5173.evil.example", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://localhost:", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://robot.example", nullptr));
    CHECK(!wsOriginAllowed("ftp://192.168.1.39", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://172.32.0.1", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://11.0.0.1", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://192.169.1.1", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://10.evil.example", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://192.168.1.300", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://192.168.1", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://evil.local.example.com", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://.local", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://192.168.1.40:", "192.168.1.39"));
    CHECK(!wsOriginAllowed("http://192.168.1.40:80x", "192.168.1.39"));
}

int main() {
    theAppTheRobotServesMayConnect();
    theAppServedLocallyForDevelopmentMayConnect();
    theAppServedFromTheLocalNetworkMayConnect();
    theHostedAppMayConnect();
    aClientWithoutAnOriginMayConnect();
    anyOtherPageMayNot();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
